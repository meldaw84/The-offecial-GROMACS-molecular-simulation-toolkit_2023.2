/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2020,2021, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
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
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*! \internal \file
 * \brief Utilities to setup GROMACS data structures for non-bonded force calculations.
 *
 * \author Victor Holanda <victor.holanda@cscs.ch>
 * \author Joe Jordan <ejjordan@kth.se>
 * \author Prashanth Kanduri <kanduri@cscs.ch>
 * \author Sebastian Keller <keller@cscs.ch>
 */
#include "gromacs/ewald/ewald_utils.h"
#include "gromacs/gpu_utils/device_stream_manager.h"
#include "gromacs/gmxlib/nrnb.h"
#include "gromacs/mdlib/forcerec.h"
#include "gromacs/mdlib/gmx_omp_nthreads.h"
#include "gromacs/mdlib/rf_util.h"
#include "gromacs/mdtypes/forcerec.h"
#include "gromacs/mdtypes/interaction_const.h"
#include "gromacs/mdtypes/simulation_workload.h"
#include "gromacs/nbnxm/atomdata.h"
#include "gromacs/nbnxm/gpu_data_mgmt.h"
#include "gromacs/nbnxm/nbnxm_gpu.h"
#include "gromacs/nbnxm/nbnxm.h"
#include "gromacs/nbnxm/nbnxm_simd.h"
#include "gromacs/nbnxm/pairlistset.h"
#include "gromacs/nbnxm/pairlistsets.h"
#include "gromacs/nbnxm/pairsearch.h"
#include "gromacs/pbcutil/pbc.h"
#include "gromacs/utility/logger.h"
#include "gromacs/utility/smalloc.h"
#include "nblib/exception.h"
#include "nblib/kerneloptions.h"
#include "nblib/particletype.h"

#include "nbnxmsetuphelpers.h"

namespace nblib
{

int64_t findNumEnergyGroups(gmx::ArrayRef<int64_t> particleInteractionFlags)
{
    auto groupId = [](int code1, int code2) {
        return (code1 & gmx::sc_atomInfo_EnergyGroupIdMask) < (code2 & gmx::sc_atomInfo_EnergyGroupIdMask);
    };

    int maxElement = *std::max_element(
            std::begin(particleInteractionFlags), std::end(particleInteractionFlags), groupId);
    return ((maxElement + 1) & gmx::sc_atomInfo_EnergyGroupIdMask);
}

Nbnxm::KernelType translateBenchmarkEnum(const SimdKernels& kernel)
{
    int kernelInt = static_cast<int>(kernel);
    return static_cast<Nbnxm::KernelType>(kernelInt);
}

void checkKernelSetup(const SimdKernels nbnxmSimd)
{
    if (nbnxmSimd >= SimdKernels::Count || nbnxmSimd == SimdKernels::SimdAuto)
    {
        throw InputException("Need a valid kernel SIMD type");
    }
    // Check SIMD support
    if ((nbnxmSimd != SimdKernels::SimdNo && !GMX_SIMD)
#ifndef GMX_NBNXN_SIMD_4XN
        || nbnxmSimd == SimdKernels::Simd4XM
#endif
#ifndef GMX_NBNXN_SIMD_2XNN
        || nbnxmSimd == SimdKernels::Simd2XMM
#endif
    )
    {
        throw InputException("The requested SIMD kernel was not set up at configuration time");
    }
}

Nbnxm::KernelSetup createKernelSetupCPU(const NBKernelOptions& options)
{
    checkKernelSetup(options.nbnxmSimd);

    Nbnxm::KernelSetup kernelSetup;

    // The int enum options.nbnxnSimd is set up to match Nbnxm::KernelType + 1
    kernelSetup.kernelType = translateBenchmarkEnum(options.nbnxmSimd);

    // The plain-C kernel does not support analytical ewald correction
    if (kernelSetup.kernelType == Nbnxm::KernelType::Cpu4x4_PlainC)
    {
        kernelSetup.ewaldExclusionType = Nbnxm::EwaldExclusionType::Table;
    }
    else
    {
        kernelSetup.ewaldExclusionType = options.useTabulatedEwaldCorr
                                                 ? Nbnxm::EwaldExclusionType::Table
                                                 : Nbnxm::EwaldExclusionType::Analytical;
    }

    return kernelSetup;
}

std::vector<int64_t> createParticleInfoAllVdv(const size_t numParticles)

{
    std::vector<int64_t> particleInfoAllVdw(numParticles);
    for (size_t particleI = 0; particleI < numParticles; particleI++)
    {
        particleInfoAllVdw[particleI] |= gmx::sc_atomInfo_HasVdw;
        particleInfoAllVdw[particleI] |= gmx::sc_atomInfo_HasCharge;
    }
    return particleInfoAllVdw;
}

std::vector<real> createNonBondedParameters(const std::vector<ParticleType>& particleTypes,
                                            const NonBondedInteractionMap& nonBondedInteractionMap)
{
    /* Todo: Refactor nbnxm to take nonbondedParameters_ directly
     *
     * initial self-handling of combination rules
     * size: 2*(numParticleTypes^2)
     */
    std::vector<real> nonbondedParameters;
    nonbondedParameters.reserve(2 * particleTypes.size() * particleTypes.size());

    constexpr real c6factor  = 6.0;
    constexpr real c12factor = 12.0;

    for (const ParticleType& particleType1 : particleTypes)
    {
        for (const ParticleType& particleType2 : particleTypes)
        {
            nonbondedParameters.push_back(
                    nonBondedInteractionMap.getC6(particleType1.name(), particleType2.name()) * c6factor);
            nonbondedParameters.push_back(
                    nonBondedInteractionMap.getC12(particleType1.name(), particleType2.name()) * c12factor);
        }
    }
    return nonbondedParameters;
}

gmx::StepWorkload createStepWorkload([[maybe_unused]] const NBKernelOptions& options)
{
    gmx::StepWorkload stepWorkload;
    stepWorkload.computeForces          = true;
    stepWorkload.computeNonbondedForces = true;
    stepWorkload.useGpuFBufferOps       = false;
    stepWorkload.useGpuXBufferOps       = false;

    return stepWorkload;
}

static gmx::SimulationWorkload createSimulationWorkload([[maybe_unused]] const NBKernelOptions& options)
{
    gmx::SimulationWorkload simulationWork;
    simulationWork.computeNonbonded = true;
    return simulationWork;
}

gmx::SimulationWorkload createSimulationWorkloadGpu(const NBKernelOptions& options)
{
    gmx::SimulationWorkload simulationWork = createSimulationWorkload(options);

    simulationWork.useGpuNonbonded = true;
    simulationWork.useGpuUpdate    = false;

    return simulationWork;
}

std::shared_ptr<gmx::DeviceStreamManager> createDeviceStreamManager(const DeviceInformation& deviceInfo,
                                                                    const gmx::SimulationWorkload& simulationWorkload)
{
    return std::make_shared<gmx::DeviceStreamManager>(deviceInfo, false, simulationWorkload, false);
}

real ewaldCoeff(const real ewald_rtol, const real pairlistCutoff)
{
    return calc_ewaldcoeff_q(pairlistCutoff, ewald_rtol);
}

interaction_const_t createInteractionConst(const NBKernelOptions& options)
{
    interaction_const_t interactionConst;
    interactionConst.vdwtype      = VanDerWaalsType::Cut;
    interactionConst.vdw_modifier = InteractionModifiers::PotShift;
    interactionConst.rvdw         = options.pairlistCutoff;

    switch (options.coulombType)
    {
        case CoulombType::Pme: interactionConst.eeltype = CoulombInteractionType::Pme; break;
        case CoulombType::Cutoff: interactionConst.eeltype = CoulombInteractionType::Cut; break;
        case CoulombType::ReactionField:
            interactionConst.eeltype = CoulombInteractionType::RF;
            break;
        case CoulombType::Count: throw InputException("Unsupported electrostatic interaction");
    }
    interactionConst.coulomb_modifier = InteractionModifiers::PotShift;
    interactionConst.rcoulomb         = options.pairlistCutoff;
    // Note: values correspond to ic->coulomb_modifier = eintmodPOTSHIFT
    interactionConst.dispersion_shift.cpot = -1.0 / gmx::power6(interactionConst.rvdw);
    interactionConst.repulsion_shift.cpot  = -1.0 / gmx::power12(interactionConst.rvdw);

    // These are the initialized values but we leave them here so that later
    // these can become options.
    interactionConst.epsilon_r                = 1.0;
    interactionConst.reactionFieldPermitivity = 1.0;

    /* Set the Coulomb energy conversion factor */
    if (interactionConst.epsilon_r != 0)
    {
        interactionConst.epsfac = ONE_4PI_EPS0 / interactionConst.epsilon_r;
    }
    else
    {
        /* eps = 0 is infinite dieletric: no Coulomb interactions */
        interactionConst.epsfac = 0;
    }

    calc_rffac(nullptr,
               interactionConst.epsilon_r,
               interactionConst.reactionFieldPermitivity,
               interactionConst.rcoulomb,
               &interactionConst.reactionFieldCoefficient,
               &interactionConst.reactionFieldShift);


    if (EEL_PME_EWALD(interactionConst.eeltype))
    {
        // Ewald coefficients, we ignore the potential shift
        interactionConst.ewaldcoeff_q = ewaldCoeff(1e-5, options.pairlistCutoff);
        if (interactionConst.ewaldcoeff_q <= 0)
        {
            throw InputException("Ewald coefficient should be > 0");
        }
        interactionConst.coulombEwaldTables = std::make_unique<EwaldCorrectionTables>();
        init_interaction_const_tables(nullptr, &interactionConst, 0, 0);
    }
    return interactionConst;
}

std::unique_ptr<nonbonded_verlet_t> createNbnxmCPU(const size_t              numParticleTypes,
                                                   const NBKernelOptions&    options,
                                                   int                       numEnergyGroups,
                                                   gmx::ArrayRef<const real> nonbondedParameters)
{
    const auto pinPolicy  = gmx::PinningPolicy::CannotBePinned;
    const int  numThreads = options.numOpenMPThreads;
    // Note: the options and Nbnxm combination rule enums values should match
    const int combinationRule = static_cast<int>(options.ljCombinationRule);

    Nbnxm::KernelSetup kernelSetup = createKernelSetupCPU(options);

    PairlistParams pairlistParams(kernelSetup.kernelType, false, options.pairlistCutoff, false);

    auto pairlistSets = std::make_unique<PairlistSets>(pairlistParams, false, 0);
    auto pairSearch   = std::make_unique<PairSearch>(
            PbcType::Xyz, false, nullptr, nullptr, pairlistParams.pairlistType, false, numThreads, pinPolicy);

    // Needs to be called with the number of unique ParticleTypes
    auto atomData = std::make_unique<nbnxn_atomdata_t>(pinPolicy,
                                                       gmx::MDLogger(),
                                                       kernelSetup.kernelType,
                                                       combinationRule,
                                                       numParticleTypes,
                                                       nonbondedParameters,
                                                       numEnergyGroups,
                                                       numThreads);

    // Put everything together
    auto nbv = std::make_unique<nonbonded_verlet_t>(
            std::move(pairlistSets), std::move(pairSearch), std::move(atomData), kernelSetup, nullptr, nullptr);

    return nbv;
}

std::unique_ptr<nonbonded_verlet_t> createNbnxmGPU(const size_t               numParticleTypes,
                                                   const NBKernelOptions&     options,
                                                   const std::vector<real>&   nonbondedParameters,
                                                   const interaction_const_t& interactionConst,
                                                   std::shared_ptr<gmx::DeviceStreamManager> deviceStreamManager)
{
    const auto pinPolicy = gmx::PinningPolicy::PinnedIfSupported;
    const int combinationRule = static_cast<int>(options.ljCombinationRule);

    Nbnxm::KernelSetup kernelSetup;
    kernelSetup.kernelType         = Nbnxm::KernelType::Gpu8x8x8;
    kernelSetup.ewaldExclusionType = options.useTabulatedEwaldCorr
                                             ? Nbnxm::EwaldExclusionType::Table
                                             : Nbnxm::EwaldExclusionType::Analytical;

    PairlistParams pairlistParams(kernelSetup.kernelType, false, options.pairlistCutoff, false);


    // nbnxn_atomdata is always initialized with 1 thread if the GPU is used
    constexpr int numThreadsInit = 1;
    // multiple energy groups are not supported on the GPU
    constexpr int numEnergyGroups = 1;
    auto          atomData        = std::make_unique<nbnxn_atomdata_t>(pinPolicy,
                                                       gmx::MDLogger(),
                                                       kernelSetup.kernelType,
                                                       combinationRule,
                                                       numParticleTypes,
                                                       nonbondedParameters,
                                                       numEnergyGroups,
                                                       numThreadsInit);

    NbnxmGpu* nbnxmGpu = Nbnxm::gpu_init(
            *deviceStreamManager, &interactionConst, pairlistParams, atomData.get(), false);

    // minimum iList count for GPU balancing
    int iListCount = Nbnxm::gpu_min_ci_balanced(nbnxmGpu);

    auto pairlistSets = std::make_unique<PairlistSets>(pairlistParams, false, iListCount);
    auto pairSearch   = std::make_unique<PairSearch>(
            PbcType::Xyz, false, nullptr, nullptr, pairlistParams.pairlistType, false, options.numOpenMPThreads, pinPolicy);

    // Put everything together
    auto nbv = std::make_unique<nonbonded_verlet_t>(
            std::move(pairlistSets), std::move(pairSearch), std::move(atomData), kernelSetup, nbnxmGpu, nullptr);
    return nbv;
}

void setGmxNonBondedNThreads(int numThreads)
{
    gmx_omp_nthreads_set(ModuleMultiThread::Pairsearch, numThreads);
    gmx_omp_nthreads_set(ModuleMultiThread::Nonbonded, numThreads);
}

void updateForcerec(t_forcerec* forcerec, const matrix& box)
{
    assert(forcerec != nullptr && "Forcerec not initialized");
    forcerec->shift_vec.resize(numShiftVectors);
    calc_shifts(box, forcerec->shift_vec);
}


} // namespace nblib
