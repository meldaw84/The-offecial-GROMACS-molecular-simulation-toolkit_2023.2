/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2015- The GROMACS Authors
 * and the project initiators Erik Lindahl, Berk Hess and David van der Spoel.
 * Consult the AUTHORS/COPYING files and https://www.gromacs.org for details.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * https://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at https://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out https://www.gromacs.org.
 */
/*! \internal \file
 * \brief Defines functionality for deciding whether tasks will run on GPUs.
 *
 * \author Mark Abraham <mark.j.abraham@gmail.com>
 * \ingroup module_taskassignment
 */

#include "gmxpre.h"

#include "gromacs/taskassignment/decidegpuusage.h"

#include "config.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <string>

#include "gromacs/ewald/pme.h"
#include "gromacs/hardware/cpuinfo.h"
#include "gromacs/hardware/detecthardware.h"
#include "gromacs/hardware/hardwaretopology.h"
#include "gromacs/hardware/hw_info.h"
#include "gromacs/listed_forces/listed_forces_gpu.h"
#include "gromacs/mdlib/gmx_omp_nthreads.h"
#include "gromacs/mdlib/update_constrain_gpu.h"
#include "gromacs/mdtypes/commrec.h"
#include "gromacs/mdtypes/inputrec.h"
#include "gromacs/mdtypes/md_enums.h"
#include "gromacs/mdtypes/mdrunoptions.h"
#include "gromacs/pulling/pull.h"
#include "gromacs/taskassignment/taskassignment.h"
#include "gromacs/topology/mtop_util.h"
#include "gromacs/topology/topology.h"
#include "gromacs/utility/baseversion.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/logger.h"
#include "gromacs/utility/message_string_collector.h"
#include "gromacs/utility/stringutil.h"


namespace gmx
{

namespace
{

//! Helper variable to localise the text of an often repeated message.
const char* const g_specifyEverythingFormatString =
        "When you use mdrun -gputasks, %s must be set to non-default "
        "values, so that the device IDs can be interpreted correctly."
#if GMX_GPU
        " If you simply want to restrict which GPUs are used, then it is "
        "better to use mdrun -gpu_id. Otherwise, setting the "
#    if GMX_GPU_CUDA
        "CUDA_VISIBLE_DEVICES"
#    elif GMX_GPU_OPENCL
        // Technically there is no portable way to do this offered by the
        // OpenCL standard, but the only current relevant case for GROMACS
        // is AMD OpenCL, which offers this variable.
        "GPU_DEVICE_ORDINAL"
#    elif GMX_GPU_SYCL && GMX_SYCL_DPCPP
        // https://github.com/intel/llvm/blob/sycl/sycl/doc/EnvironmentVariables.md
        "SYCL_DEVICE_FILTER"
#    elif GMX_GPU_SYCL && GMX_SYCL_HIPSYCL
        // Not true if we use hipSYCL over CUDA or IntelLLVM, but in that case the user probably
        // knows what they are doing.
        // https://rocmdocs.amd.com/en/latest/Other_Solutions/Other-Solutions.html#hip-environment-variables
        "HIP_VISIBLE_DEVICES"
#    else
#        error "Unreachable branch"
#    endif
        " environment variable in your bash profile or job "
        "script may be more convenient."
#endif
        ;

// The conditions below must be in sync with getSkipMessagesIfNecessary check in src/programs/mdrun/tests/pmetest.cpp
constexpr bool c_gpuBuildSyclWithoutGpuFft =
        (GMX_GPU_SYCL != 0) && (GMX_GPU_FFT_MKL == 0) && (GMX_GPU_FFT_ROCFFT == 0)
        && (GMX_GPU_FFT_VKFFT == 0); // NOLINT(misc-redundant-expression)
} // namespace

bool decideWhetherToUseGpusForNonbondedWithThreadMpi(const TaskTarget        nonbondedTarget,
                                                     const bool              haveAvailableDevices,
                                                     const std::vector<int>& userGpuTaskAssignment,
                                                     const EmulateGpuNonbonded emulateGpuNonbonded,
                                                     const bool buildSupportsNonbondedOnGpu,
                                                     const bool nonbondedOnGpuIsUseful,
                                                     const int  numRanksPerSimulation)
{
    // First, exclude all cases where we can't run NB on GPUs.
    if (nonbondedTarget == TaskTarget::Cpu || emulateGpuNonbonded == EmulateGpuNonbonded::Yes
        || !nonbondedOnGpuIsUseful || !buildSupportsNonbondedOnGpu)
    {
        // If the user required NB on GPUs, we issue an error later.
        return false;
    }

    // We now know that NB on GPUs makes sense, if we have any.

    if (!userGpuTaskAssignment.empty())
    {
        // Specifying -gputasks requires specifying everything.
        if (nonbondedTarget == TaskTarget::Auto || numRanksPerSimulation < 1)
        {
            GMX_THROW(InconsistentInputError(
                    formatString(g_specifyEverythingFormatString, "-nb and -ntmpi")));
        }
        return true;
    }

    if (nonbondedTarget == TaskTarget::Gpu)
    {
        return true;
    }

    // Because this is thread-MPI, we already know about the GPUs that
    // all potential ranks can use, and can use that in a global
    // decision that will later be consistent.
    // If we get here, then the user permitted or required GPUs.
    return haveAvailableDevices;
}

static bool decideWhetherToUseGpusForPmeFft(const TaskTarget pmeFftTarget)
{
    const bool useCpuFft = (pmeFftTarget == TaskTarget::Cpu)
                           || (pmeFftTarget == TaskTarget::Auto && c_gpuBuildSyclWithoutGpuFft);
    return !useCpuFft;
}

static bool canUseGpusForPme(const bool                      useGpuForNonbonded,
                             const TaskTarget                pmeTarget,
                             const TaskTarget                pmeFftTarget,
                             const gmx_hw_info_t& gmx_unused hardwareInfo,
                             const t_inputrec&               inputrec,
                             std::string*                    errorMessage)
{
    if (pmeTarget == TaskTarget::Cpu)
    {
        return false;
    }

    std::string                 tempString;
    gmx::MessageStringCollector errorReasons;
    // Before changing the prefix string, make sure that it is not searched for in regression tests.
    errorReasons.startContext("Cannot compute PME interactions on a GPU, because:");
    errorReasons.appendIf(!useGpuForNonbonded, "Nonbonded interactions must also run on GPUs.");
    errorReasons.appendIf(!pme_gpu_supports_build(&tempString), tempString);
    errorReasons.appendIf(!pme_gpu_supports_input(inputrec, &tempString), tempString);
    if (!decideWhetherToUseGpusForPmeFft(pmeFftTarget))
    {
        // We need to do FFT on CPU, so we check whether we are able to use PME Mixed mode.
        errorReasons.appendIf(!pme_gpu_mixed_mode_supports_input(inputrec, &tempString), tempString);
    }
    errorReasons.finishContext();

    if (errorReasons.isEmpty())
    {
        return true;
    }
    else
    {
        if (pmeTarget == TaskTarget::Gpu && errorMessage != nullptr)
        {
            *errorMessage = errorReasons.toString();
        }
        return false;
    }
}

bool decideWhetherToUseGpusForPmeWithThreadMpi(const bool              useGpuForNonbonded,
                                               const TaskTarget        pmeTarget,
                                               const TaskTarget        pmeFftTarget,
                                               const int               numDevicesToUse,
                                               const std::vector<int>& userGpuTaskAssignment,
                                               const gmx_hw_info_t&    hardwareInfo,
                                               const t_inputrec&       inputrec,
                                               const int               numRanksPerSimulation,
                                               const int               numPmeRanksPerSimulation)
{
    // First, exclude all cases where we can't run PME on GPUs.
    if (!canUseGpusForPme(useGpuForNonbonded, pmeTarget, pmeFftTarget, hardwareInfo, inputrec, nullptr))
    {
        // PME can't run on a GPU. If the user required that, we issue an error later.
        return false;
    }

    // We now know that PME on GPUs might make sense, if we have any.

    if (pmeTarget == TaskTarget::Gpu)
    {
        if ((numRanksPerSimulation > 1) && (numPmeRanksPerSimulation < 0))
        {
            GMX_THROW(NotImplementedError(
                    "PME tasks were required to run on GPUs with multiple ranks "
                    "but the -npme option was not specified. "
                    "A non-negative value must be specified for -npme."));
        }
    }

    if (!userGpuTaskAssignment.empty())
    {
        // Follow the user's choice of GPU task assignment, if we
        // can. Checking that their IDs are for compatible GPUs comes
        // later.

        // Specifying -gputasks requires specifying everything.
        if (pmeTarget == TaskTarget::Auto || numRanksPerSimulation < 1)
        {
            GMX_THROW(InconsistentInputError(
                    formatString(g_specifyEverythingFormatString, "all of -nb, -pme, and -ntmpi")));
        }

        // PME on GPUs is only supported in a single case
        if (pmeTarget == TaskTarget::Gpu)
        {
            if (((numRanksPerSimulation > 1) && (numPmeRanksPerSimulation == 0))
                || (numPmeRanksPerSimulation > 1))
            {
                GMX_THROW(InconsistentInputError(
                        "When you run mdrun -pme gpu -gputasks, you must supply a PME-enabled .tpr "
                        "file and use a single PME rank."));
            }
            return true;
        }

        // pmeTarget == TaskTarget::Auto
        return numRanksPerSimulation == 1;
    }

    // Because this is thread-MPI, we already know about the GPUs that
    // all potential ranks can use, and can use that in a global
    // decision that will later be consistent.

    if (pmeTarget == TaskTarget::Gpu)
    {
        if (((numRanksPerSimulation > 1) && (numPmeRanksPerSimulation == 0))
            || (numPmeRanksPerSimulation > 1))
        {
            GMX_THROW(NotImplementedError(
                    "PME tasks were required to run on GPUs, but that is not implemented with "
                    "more than one PME rank. Use a single rank simulation, or a separate PME rank, "
                    "or permit PME tasks to be assigned to the CPU."));
        }
        return true;
    }

    if (numRanksPerSimulation == 1)
    {
        // PME can run well on a GPU shared with NB, and we permit
        // mdrun to default to try that.
        return numDevicesToUse > 0;
    }

    if (numPmeRanksPerSimulation == 1)
    {
        // We have a single separate PME rank, that can use a GPU
        return numDevicesToUse > 0;
    }

    if (numRanksPerSimulation < 1)
    {
        // Full automated mode for thread-MPI (the default). PME can
        // run well on a GPU shared with NB, and we permit mdrun to
        // default to it if there is only one GPU available.
        return (numDevicesToUse == 1);
    }

    // Not enough support for PME on GPUs for anything else
    return false;
}

bool decideWhetherToUseGpusForNonbonded(const TaskTarget          nonbondedTarget,
                                        const std::vector<int>&   userGpuTaskAssignment,
                                        const EmulateGpuNonbonded emulateGpuNonbonded,
                                        const bool                buildSupportsNonbondedOnGpu,
                                        const bool                nonbondedOnGpuIsUseful,
                                        const bool                gpusWereDetected)
{
    if (nonbondedTarget == TaskTarget::Cpu)
    {
        if (!userGpuTaskAssignment.empty())
        {
            GMX_THROW(InconsistentInputError(
                    "A GPU task assignment was specified, but nonbonded interactions were "
                    "assigned to the CPU. Make no more than one of these choices."));
        }

        return false;
    }

    if (!buildSupportsNonbondedOnGpu && nonbondedTarget == TaskTarget::Gpu)
    {
        GMX_THROW(InconsistentInputError(
                "Nonbonded interactions on the GPU were requested with -nb gpu, "
                "but the GROMACS binary has been built without GPU support. "
                "Either run without selecting GPU options, or recompile GROMACS "
                "with GPU support enabled"));
    }

    // TODO refactor all these TaskTarget::Gpu checks into one place?
    // e.g. use a subfunction that handles only the cases where
    // TaskTargets are not Cpu?
    if (emulateGpuNonbonded == EmulateGpuNonbonded::Yes)
    {
        if (nonbondedTarget == TaskTarget::Gpu)
        {
            GMX_THROW(InconsistentInputError(
                    "Nonbonded interactions on the GPU were required, which is inconsistent "
                    "with choosing emulation. Make no more than one of these choices."));
        }
        if (!userGpuTaskAssignment.empty())
        {
            GMX_THROW(
                    InconsistentInputError("GPU ID usage was specified, as was GPU emulation. Make "
                                           "no more than one of these choices."));
        }

        return false;
    }

    if (!nonbondedOnGpuIsUseful)
    {
        if (nonbondedTarget == TaskTarget::Gpu)
        {
            GMX_THROW(InconsistentInputError(
                    "Nonbonded interactions on the GPU were required, but not supported for these "
                    "simulation settings. Change your settings, or do not require using GPUs."));
        }

        return false;
    }

    if (!userGpuTaskAssignment.empty())
    {
        // Specifying -gputasks requires specifying everything.
        if (nonbondedTarget == TaskTarget::Auto)
        {
            GMX_THROW(InconsistentInputError(
                    formatString(g_specifyEverythingFormatString, "-nb and -ntmpi")));
        }

        return true;
    }

    if (nonbondedTarget == TaskTarget::Gpu)
    {
        // We still don't know whether it is an error if no GPUs are found
        // because we don't know the duty of this rank, yet. For example,
        // a node with only PME ranks and -pme cpu is OK if there are not
        // GPUs.
        return true;
    }

    // If we get here, then the user permitted GPUs, which we should
    // use for nonbonded interactions.
    return buildSupportsNonbondedOnGpu && gpusWereDetected;
}

bool decideWhetherToUseGpusForPme(const bool              useGpuForNonbonded,
                                  const TaskTarget        pmeTarget,
                                  const TaskTarget        pmeFftTarget,
                                  const std::vector<int>& userGpuTaskAssignment,
                                  const gmx_hw_info_t&    hardwareInfo,
                                  const t_inputrec&       inputrec,
                                  const int               numRanksPerSimulation,
                                  const int               numPmeRanksPerSimulation,
                                  const bool              gpusWereDetected)
{
    std::string message;
    if (!canUseGpusForPme(useGpuForNonbonded, pmeTarget, pmeFftTarget, hardwareInfo, inputrec, &message))
    {
        if (!message.empty())
        {
            GMX_THROW(InconsistentInputError(message));
        }
        return false;
    }

    if (pmeTarget == TaskTarget::Cpu)
    {
        if (!userGpuTaskAssignment.empty())
        {
            GMX_THROW(InconsistentInputError(
                    "A GPU task assignment was specified, but PME interactions were "
                    "assigned to the CPU. Make no more than one of these choices."));
        }

        return false;
    }

    if (pmeTarget == TaskTarget::Gpu)
    {
        if ((numRanksPerSimulation > 1) && (numPmeRanksPerSimulation < 0))
        {
            GMX_THROW(NotImplementedError(
                    "PME tasks were required to run on GPUs with multiple ranks "
                    "but the -npme option was not specified. "
                    "A non-negative value must be specified for -npme."));
        }
    }

    if (!userGpuTaskAssignment.empty())
    {
        // Specifying -gputasks requires specifying everything.
        if (pmeTarget == TaskTarget::Auto)
        {
            GMX_THROW(InconsistentInputError(formatString(
                    g_specifyEverythingFormatString, "all of -nb, -pme, and -ntmpi"))); // TODO ntmpi?
        }

        return true;
    }

    // We still don't know whether it is an error if no GPUs are found
    // because we don't know the duty of this rank, yet. For example,
    // a node with only PME ranks and -pme cpu is OK if there are not
    // GPUs.

    if (pmeTarget == TaskTarget::Gpu)
    {
        return true;
    }

    // If we get here, then the user permitted GPUs.
    if (numRanksPerSimulation == 1)
    {
        // PME can run well on a single GPU shared with NB when there
        // is one rank, so we permit mdrun to try that if we have
        // detected GPUs.
        return gpusWereDetected;
    }

    if (numPmeRanksPerSimulation == 1)
    {
        // We have a single separate PME rank, that can use a GPU
        return gpusWereDetected;
    }

    // Not enough support for PME on GPUs for anything else
    return false;
}


PmeRunMode determinePmeRunMode(const bool useGpuForPme, const TaskTarget& pmeFftTarget, const t_inputrec& inputrec)
{
    if (!usingPme(inputrec.coulombtype) && !usingLJPme(inputrec.vdwtype))
    {
        return PmeRunMode::None;
    }

    if (useGpuForPme)
    {
        if (c_gpuBuildSyclWithoutGpuFft && pmeFftTarget == TaskTarget::Gpu)
        {
            gmx_fatal(FARGS,
                      "GROMACS is built without SYCL GPU FFT library. Please use -pmefft cpu.");
        }
        if (!decideWhetherToUseGpusForPmeFft(pmeFftTarget))
        {
            return PmeRunMode::Mixed;
        }
        else
        {
            return PmeRunMode::GPU;
        }
    }
    else
    {
        if (pmeFftTarget == TaskTarget::Gpu)
        {
            gmx_fatal(FARGS,
                      "Assigning FFTs to GPU requires PME to be assigned to GPU as well. With PME "
                      "on CPU you should not be using -pmefft.");
        }
        return PmeRunMode::CPU;
    }
}

bool decideWhetherToUseGpusForBonded(bool              useGpuForNonbonded,
                                     bool              useGpuForPme,
                                     TaskTarget        bondedTarget,
                                     const t_inputrec& inputrec,
                                     const gmx_mtop_t& mtop,
                                     int               numPmeRanksPerSimulation,
                                     bool              gpusWereDetected)
{
    if (bondedTarget == TaskTarget::Cpu)
    {
        return false;
    }

    std::string errorMessage;

    if (!buildSupportsListedForcesGpu(&errorMessage))
    {
        if (bondedTarget == TaskTarget::Gpu)
        {
            GMX_THROW(InconsistentInputError(errorMessage.c_str()));
        }

        return false;
    }

    if (!inputSupportsListedForcesGpu(inputrec, mtop, &errorMessage))
    {
        if (bondedTarget == TaskTarget::Gpu)
        {
            GMX_THROW(InconsistentInputError(errorMessage.c_str()));
        }

        return false;
    }

    if (!useGpuForNonbonded)
    {
        if (bondedTarget == TaskTarget::Gpu)
        {
            GMX_THROW(InconsistentInputError(
                    "Bonded interactions on the GPU were required, but this requires that "
                    "short-ranged non-bonded interactions are also run on the GPU. Change "
                    "your settings, or do not require using GPUs."));
        }

        return false;
    }

    // TODO If the bonded kernels do not get fused, then performance
    // overheads might suggest alternative choices here.

    if (bondedTarget == TaskTarget::Gpu)
    {
        // We still don't know whether it is an error if no GPUs are
        // found.
        return true;
    }

    // If we get here, then the user permitted GPUs, which we should
    // use for bonded interactions if any were detected and the CPU
    // is busy, for which we currently only check PME or Ewald.
    // (It would be better to dynamically assign bondeds based on timings)
    // Note that here we assume that the auto setting of PME ranks will not
    // choose separate PME ranks when nonBonded are assigned to the GPU.
    bool usingOurCpuForPmeOrEwald = (usingLJPme(inputrec.vdwtype)
                                     || (usingPmeOrEwald(inputrec.coulombtype) && !useGpuForPme
                                         && numPmeRanksPerSimulation <= 0));

    return gpusWereDetected && usingOurCpuForPmeOrEwald;
}

bool decideWhetherToUseGpuForUpdate(const bool                     isDomainDecomposition,
                                    const bool                     useUpdateGroups,
                                    const PmeRunMode               pmeRunMode,
                                    const bool                     havePmeOnlyRank,
                                    const bool                     useGpuForNonbonded,
                                    const TaskTarget               updateTarget,
                                    const bool                     gpusWereDetected,
                                    const t_inputrec&              inputrec,
                                    const gmx_mtop_t&              mtop,
                                    const bool                     useEssentialDynamics,
                                    const bool                     doOrientationRestraints,
                                    const bool                     haveFrozenAtoms,
                                    const bool                     doRerun,
                                    const DevelopmentFeatureFlags& devFlags,
                                    const gmx::MDLogger&           mdlog)
{

    // '-update cpu' overrides the environment variable, '-update auto' does not
    if (updateTarget == TaskTarget::Cpu
        || (updateTarget == TaskTarget::Auto && !devFlags.forceGpuUpdateDefault))
    {
        return false;
    }

    const bool hasAnyConstraints      = gmx_mtop_interaction_count(mtop, IF_CONSTRAINT) > 0;
    const bool pmeSpreadGatherUsesCpu = (pmeRunMode == PmeRunMode::CPU);

    std::string errorMessage;

    if (isDomainDecomposition)
    {
        if (hasAnyConstraints && !useUpdateGroups)
        {
            errorMessage +=
                    "Domain decomposition is only supported with constraints when update "
                    "groups "
                    "are used. This means constraining all bonds is not supported, except for "
                    "small molecules, and box sizes close to half the pair-list cutoff are not "
                    "supported.\n ";
        }
    }

    if (havePmeOnlyRank)
    {
        if (pmeSpreadGatherUsesCpu)
        {
            errorMessage += "With separate PME rank(s), PME must run on the GPU.\n";
        }
    }

    if (inputrec.useMts)
    {
        errorMessage += "Multiple time stepping is not supported.\n";
    }

    if (inputrec.eConstrAlg == ConstraintAlgorithm::Shake && hasAnyConstraints
        && gmx_mtop_ftype_count(mtop, F_CONSTR) > 0)
    {
        errorMessage += "SHAKE constraints are not supported.\n";
    }
    // Using the GPU-version of update if:
    // 1. PME is on the GPU (there should be a copy of coordinates on GPU for PME spread) or inactive, or
    // 2. Non-bonded interactions are on the GPU.
    if ((pmeRunMode == PmeRunMode::CPU || pmeRunMode == PmeRunMode::None) && !useGpuForNonbonded)
    {
        errorMessage +=
                "Either PME or short-ranged non-bonded interaction tasks must run on the GPU.\n";
    }
    if (!gpusWereDetected)
    {
        errorMessage += "Compatible GPUs must have been found.\n";
    }
    if (!(GMX_GPU_CUDA || GMX_GPU_SYCL))
    {
        errorMessage += "Only CUDA and SYCL builds are supported.\n";
    }
    if (inputrec.eI != IntegrationAlgorithm::MD)
    {
        errorMessage += "Only the md integrator is supported.\n";
    }
    if (inputrec.etc == TemperatureCoupling::NoseHoover)
    {
        errorMessage += "Nose-Hoover temperature coupling is not supported.\n";
    }
    if (!(inputrec.pressureCouplingOptions.epc == PressureCoupling::No
          || inputrec.pressureCouplingOptions.epc == PressureCoupling::ParrinelloRahman
          || inputrec.pressureCouplingOptions.epc == PressureCoupling::Berendsen
          || inputrec.pressureCouplingOptions.epc == PressureCoupling::CRescale))
    {
        errorMessage +=
                "Only Parrinello-Rahman, Berendsen, and C-rescale pressure coupling are "
                "supported.\n";
    }
    if (inputrec.cos_accel != 0 || inputrec.useConstantAcceleration)
    {
        errorMessage += "Acceleration is not supported.\n";
    }
    if (usingPmeOrEwald(inputrec.coulombtype) && inputrec.epsilon_surface != 0)
    {
        // The graph is needed, but not supported
        errorMessage += "Ewald surface correction is not supported.\n";
    }
    if (gmx_mtop_interaction_count(mtop, IF_VSITE) > 0)
    {
        errorMessage += "Virtual sites are not supported.\n";
    }
    if (useEssentialDynamics)
    {
        errorMessage += "Essential dynamics is not supported.\n";
    }
    if (inputrec.bPull && pull_have_constraint(*inputrec.pull))
    {
        errorMessage += "Constraints pulling is not supported.\n";
    }
    if (doOrientationRestraints)
    {
        // The graph is needed, but not supported
        errorMessage += "Orientation restraints are not supported.\n";
    }
    if (inputrec.efep != FreeEnergyPerturbationType::No
        && (haveFepPerturbedMasses(mtop) || havePerturbedConstraints(mtop)))
    {
        errorMessage += "Free energy perturbation for mass and constraints are not supported.\n";
    }
    const auto particleTypes = gmx_mtop_particletype_count(mtop);
    if (particleTypes[ParticleType::Shell] > 0)
    {
        errorMessage += "Shells are not supported.\n";
    }
    if (inputrec.eSwapCoords != SwapType::No)
    {
        errorMessage += "Swapping the coordinates is not supported.\n";
    }
    if (doRerun)
    {
        errorMessage += "Re-run is not supported.\n";
    }

    // TODO: F_CONSTRNC is only unsupported, because isNumCoupledConstraintsSupported()
    // does not support it, the actual CUDA LINCS code does support it
    if (gmx_mtop_ftype_count(mtop, F_CONSTRNC) > 0)
    {
        errorMessage += "Non-connecting constraints are not supported\n";
    }
    if (!UpdateConstrainGpu::isNumCoupledConstraintsSupported(mtop))
    {
        errorMessage +=
                "The number of coupled constraints is higher than supported in the GPU LINCS "
                "code.\n";
    }
    if (hasAnyConstraints && !UpdateConstrainGpu::areConstraintsSupported())
    {
        errorMessage += "Chosen GPU implementation does not support constraints.\n";
    }
    if (haveFrozenAtoms)
    {
        // There is a known bug with frozen atoms and GPU update, see Issue #3920.
        errorMessage += "Frozen atoms not supported.\n";
    }

    if (!errorMessage.empty())
    {
        if (updateTarget == TaskTarget::Auto && devFlags.forceGpuUpdateDefault)
        {
            GMX_LOG(mdlog.warning)
                    .asParagraph()
                    .appendText(
                            "Update task on the GPU was required, by the "
                            "GMX_FORCE_UPDATE_DEFAULT_GPU environment variable, but the following "
                            "condition(s) were not satisfied:");
            GMX_LOG(mdlog.warning).asParagraph().appendText(errorMessage.c_str());
            GMX_LOG(mdlog.warning).asParagraph().appendText("Will use CPU version of update.");
        }
        else if (updateTarget == TaskTarget::Gpu)
        {
            std::string prefix = gmx::formatString(
                    "Update task on the GPU was required,\n"
                    "but the following condition(s) were not satisfied:\n");
            GMX_THROW(InconsistentInputError((prefix + errorMessage).c_str()));
        }
        return false;
    }

    return (updateTarget == TaskTarget::Gpu
            || (updateTarget == TaskTarget::Auto && devFlags.forceGpuUpdateDefault));
}

bool decideWhetherDirectGpuCommunicationCanBeUsed(const DevelopmentFeatureFlags& devFlags,
                                                  bool                           haveMts,
                                                  bool                           haveSwapCoords,
                                                  const gmx::MDLogger&           mdlog)
{
    const bool buildSupportsDirectGpuComm = (GMX_GPU_CUDA || GMX_GPU_SYCL) && GMX_MPI;
    if (!buildSupportsDirectGpuComm)
    {
        return false;
    }

    // Direct GPU communication is presently turned off due to insufficient testing
    const bool enableDirectGpuComm = (getenv("GMX_ENABLE_DIRECT_GPU_COMM") != nullptr)
                                     || (getenv("GMX_GPU_DD_COMMS") != nullptr)
                                     || (getenv("GMX_GPU_PME_PP_COMMS") != nullptr);

    if (GMX_THREAD_MPI && GMX_GPU_SYCL && enableDirectGpuComm)
    {
        GMX_LOG(mdlog.warning)
                .asParagraph()
                .appendTextFormatted(
                        "GMX_ENABLE_DIRECT_GPU_COMM environment variable detected, "
                        "but SYCL does not support direct communications with threadMPI.");
    }

    // Now check those flags that may cause, from the user perspective, an unexpected
    // fallback to CPU halo, and report accordingly
    gmx::MessageStringCollector errorReasons;
    errorReasons.startContext("GPU direct communication can not be activated because:");
    errorReasons.appendIf(haveMts, "MTS is not supported.");
    errorReasons.appendIf(haveSwapCoords, "Swap-coords is not supported.");
    errorReasons.finishContext();

    if (!errorReasons.isEmpty())
    {
        GMX_LOG(mdlog.warning).asParagraph().appendText(errorReasons.toString());
    }

    bool runUsesCompatibleFeatures = errorReasons.isEmpty();

    bool runAndGpuSupportDirectGpuComm = (runUsesCompatibleFeatures && enableDirectGpuComm);

    // Thread-MPI case on by default, can be disabled with env var.
    bool canUseDirectGpuCommWithThreadMpi =
            (runAndGpuSupportDirectGpuComm && GMX_THREAD_MPI && !GMX_GPU_SYCL);
    // GPU-aware MPI case off by default, can be enabled with dev flag
    // Note: GMX_DISABLE_DIRECT_GPU_COMM already taken into account in devFlags.enableDirectGpuCommWithMpi
    bool canUseDirectGpuCommWithMpi = (runAndGpuSupportDirectGpuComm && GMX_LIB_MPI
                                       && devFlags.canUseGpuAwareMpi && enableDirectGpuComm);

    return canUseDirectGpuCommWithThreadMpi || canUseDirectGpuCommWithMpi;
}

bool decideWhetherToUseGpuForHalo(bool                 havePPDomainDecomposition,
                                  bool                 useGpuForNonbonded,
                                  bool                 canUseDirectGpuComm,
                                  bool                 useModularSimulator,
                                  bool                 doRerun,
                                  bool                 haveEnergyMinimization,
                                  const gmx::MDLogger& mdlog)
{
    if (!canUseDirectGpuComm || !havePPDomainDecomposition || !useGpuForNonbonded)
    {
        // return false without warning
        return false;
    }

    // Now check those flags that may cause, from the user perspective, an unexpected
    // fallback to CPU halo, and report accordingly
    gmx::MessageStringCollector errorReasons;
    errorReasons.startContext("GPU halo exchange will not be activated because:");
    errorReasons.appendIf(useModularSimulator, "Modular simulator runs are not supported.");
    errorReasons.appendIf(doRerun, "Re-runs are not supported.");
    errorReasons.appendIf(haveEnergyMinimization, "Energy minimization is not supported.");
    errorReasons.finishContext();

    if (!errorReasons.isEmpty())
    {
        GMX_LOG(mdlog.warning).asParagraph().appendText(errorReasons.toString());
    }

    return errorReasons.isEmpty();
}

} // namespace gmx
