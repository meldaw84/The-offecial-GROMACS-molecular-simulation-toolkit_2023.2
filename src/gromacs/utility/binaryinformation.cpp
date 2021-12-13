/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014,2015,2016,2017 by the GROMACS development team.
 * Copyright (c) 2018,2019,2020,2021, by the GROMACS development team, led by
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
 * \brief Implements functionality for printing information about the
 * currently running binary
 *
 * \ingroup module_utility
 */
#include "gmxpre.h"

#include "binaryinformation.h"

#include "config.h"

#if GMX_FFT_FFTW3 || GMX_FFT_ARMPL_FFTW3
// Needed for construction of the FFT library description string
#    include <fftw3.h>
#endif

#if HAVE_LIBMKL
#    include <mkl.h>
#endif

#if HAVE_EXTRAE
#    include <extrae_user_events.h>
#endif

#if GMX_USE_HWLOC
#    include <hwloc.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <array>
#include <string>

/* This file is completely threadsafe - keep it that way! */

#include "buildinfo.h"
#include "gromacs/utility/arraysize.h"
#include "gromacs/utility/baseversion.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/path.h"
#include "gromacs/utility/programcontext.h"
#include "gromacs/utility/stringutil.h"
#include "gromacs/utility/sysinfo.h"
#include "gromacs/utility/textwriter.h"

#include "cuda_version_information.h"
#include "sycl_version_information.h"

namespace
{

using gmx::formatString;

//! \cond Doxygen does not need to care about most of this stuff, and the macro usage is painful to document

int centeringOffset(int width, int length)
{
    return std::max(width - length, 0) / 2;
}

std::string formatCentered(int width, const char* text)
{
    const int offset = centeringOffset(width, std::strlen(text));
    return formatString("%*s%s", offset, "", text);
}

//! Construct a string that describes the library that provides CPU FFT support to this build
const char* getCpuFftDescriptionString()
{
// Define the FFT description string
#if GMX_FFT_FFTW3 || GMX_FFT_ARMPL_FFTW3
#    if GMX_NATIVE_WINDOWS
    // Don't buy trouble
    return "fftw3";
#    else
    // Use the version string provided by libfftw3
#        if GMX_DOUBLE
    return fftw_version;
#        else
    return fftwf_version;
#        endif
#    endif
#endif
#if GMX_FFT_MKL
    return "Intel MKL";
#endif
#if GMX_FFT_FFTPACK
    return "fftpack (built-in)";
#endif
};

//! Construct a string that describes the library that provides GPU FFT support to this build
const char* getGpuFftDescriptionString()
{
    if (GMX_GPU)
    {
        if (GMX_GPU_CUDA)
        {
            return "cuFFT";
        }
        else if (GMX_GPU_OPENCL)
        {
            return "clFFT";
        }
        else if (GMX_GPU_SYCL)
        {
            return "unknown";
        }
        else
        {
            GMX_RELEASE_ASSERT(false, "Unknown GPU configuration");
            return "impossible";
        }
    }
    else
    {
        return "none";
    }
};

void gmx_print_version_info(gmx::TextWriter* writer)
{
    writer->writeLine(formatString("GROMACS version:    %s", gmx_version()));
    const char* const git_hash = gmx_version_git_full_hash();
    if (git_hash[0] != '\0')
    {
        writer->writeLine(formatString("GIT SHA1 hash:      %s", git_hash));
    }
    const char* const base_hash = gmx_version_git_central_base_hash();
    if (base_hash[0] != '\0')
    {
        writer->writeLine(formatString("Branched from:      %s", base_hash));
    }


#if GMX_DOUBLE
    writer->writeLine("Precision:          double");
#else
    writer->writeLine("Precision:          mixed");
#endif
    writer->writeLine(formatString("Memory model:       %u bit", static_cast<unsigned>(8 * sizeof(void*))));

#if GMX_THREAD_MPI
    writer->writeLine("MPI library:        thread_mpi");
#elif GMX_MPI
#    if HAVE_CUDA_AWARE_MPI
    writer->writeLine("MPI library:        MPI (CUDA-aware)");
#    else
    writer->writeLine("MPI library:        MPI");
#    endif
#else
    writer->writeLine("MPI library:        none");
#endif
#if GMX_OPENMP
    writer->writeLine(formatString("OpenMP support:     enabled (GMX_OPENMP_MAX_THREADS = %d)",
                                   GMX_OPENMP_MAX_THREADS));
#else
    writer->writeLine("OpenMP support:     disabled");
#endif
    writer->writeLine(formatString("GPU support:        %s", getGpuImplementationString()));
    writer->writeLine(formatString("SIMD instructions:  %s", GMX_SIMD_STRING));
    writer->writeLine(formatString("CPU FFT library:    %s", getCpuFftDescriptionString()));
    writer->writeLine(formatString("GPU FFT library:    %s", getGpuFftDescriptionString()));
#if GMX_TARGET_X86
    writer->writeLine(formatString("RDTSCP usage:       %s", GMX_USE_RDTSCP ? "enabled" : "disabled"));
#endif
#if GMX_USE_TNG
    writer->writeLine("TNG support:        enabled");
#else
    writer->writeLine("TNG support:        disabled");
#endif
#if GMX_USE_HWLOC
    writer->writeLine(formatString("Hwloc support:      hwloc-%s", HWLOC_VERSION));
#else
    writer->writeLine("Hwloc support:      disabled");
#endif
#if HAVE_EXTRAE
    unsigned major, minor, revision;
    Extrae_get_version(&major, &minor, &revision);
    writer->writeLine(formatString(
            "Tracing support:    enabled. Using Extrae-%d.%d.%d", major, minor, revision));
#else
    writer->writeLine("Tracing support:    disabled");
#endif


    /* TODO: The below strings can be quite long, so it would be nice to wrap
     * them. Can wait for later, as the master branch has ready code to do all
     * that. */
    writer->writeLine(formatString("C compiler:         %s", BUILD_C_COMPILER));
    writer->writeLine(formatString(
            "C compiler flags:   %s %s", BUILD_CFLAGS, CMAKE_BUILD_CONFIGURATION_C_FLAGS));
    writer->writeLine(formatString("C++ compiler:       %s", BUILD_CXX_COMPILER));
    writer->writeLine(formatString(
            "C++ compiler flags: %s %s", BUILD_CXXFLAGS, CMAKE_BUILD_CONFIGURATION_CXX_FLAGS));
#if HAVE_LIBMKL
    /* MKL might be used for LAPACK/BLAS even if FFTs use FFTW, so keep it separate */
    writer->writeLine(formatString(
            "Intel MKL version:  %d.%d.%d", __INTEL_MKL__, __INTEL_MKL_MINOR__, __INTEL_MKL_UPDATE__));
#endif
#if GMX_GPU_OPENCL
    writer->writeLine(formatString("OpenCL include dir: %s", OPENCL_INCLUDE_DIR));
    writer->writeLine(formatString("OpenCL library:     %s", OPENCL_LIBRARY));
    writer->writeLine(formatString("OpenCL version:     %s", OPENCL_VERSION_STRING));
#endif
#if GMX_GPU_CUDA
    writer->writeLine(formatString("CUDA compiler:      %s", CUDA_COMPILER_INFO));
    writer->writeLine(formatString(
            "CUDA compiler flags:%s %s", CUDA_COMPILER_FLAGS, CMAKE_BUILD_CONFIGURATION_CXX_FLAGS));
    writer->writeLine("CUDA driver:        " + gmx::getCudaDriverVersionString());
    writer->writeLine("CUDA runtime:       " + gmx::getCudaRuntimeVersionString());
#endif
#if GMX_SYCL_DPCPP
    writer->writeLine(formatString("SYCL DPC++ flags:   %s", SYCL_DPCPP_COMPILER_FLAGS));
    writer->writeLine("SYCL DPC++ version: " + gmx::getSyclCompilerVersion());
#endif
#if GMX_SYCL_HIPSYCL
    writer->writeLine(formatString("hipSYCL launcher:   %s", SYCL_HIPSYCL_COMPILER_LAUNCHER));
    writer->writeLine(formatString("hipSYCL flags:      %s", SYCL_HIPSYCL_COMPILER_FLAGS));
    writer->writeLine(formatString("hipSYCL targets:    %s", SYCL_HIPSYCL_TARGETS));
    writer->writeLine("hipSYCL version:    " + gmx::getSyclCompilerVersion());
#endif
}

//! \endcond

} // namespace

namespace gmx
{

BinaryInformationSettings::BinaryInformationSettings() :
    bExtendedInfo_(false), bProcessId_(false), bGeneratedByHeader_(false), prefix_(""), suffix_("")
{
}

void printBinaryInformation(FILE* fp, const IProgramContext& programContext)
{
    TextWriter writer(fp);
    printBinaryInformation(&writer, programContext, BinaryInformationSettings());
}

void printBinaryInformation(FILE*                            fp,
                            const IProgramContext&           programContext,
                            const BinaryInformationSettings& settings)
{
    try
    {
        TextWriter writer(fp);
        printBinaryInformation(&writer, programContext, settings);
    }
    GMX_CATCH_ALL_AND_EXIT_WITH_FATAL_ERROR
}

void printBinaryInformation(TextWriter*                      writer,
                            const IProgramContext&           programContext,
                            const BinaryInformationSettings& settings)
{
    // TODO Perhaps the writer could be configured with the prefix and
    // suffix strings from the settings?
    const char* prefix          = settings.prefix_;
    const char* suffix          = settings.suffix_;
    const char* precisionString = "";
#if GMX_DOUBLE
    precisionString = " (double precision)";
#endif
    const char* const name = programContext.displayName();
    if (settings.bGeneratedByHeader_)
    {
        writer->writeLine(formatString("%sCreated by:%s", prefix, suffix));
    }
    // TODO: It would be nice to know here whether we are really running a
    // Gromacs binary or some other binary that is calling Gromacs; we
    // could then print "%s is part of GROMACS" or some alternative text.
    std::string title = formatString(":-) GROMACS - %s, %s%s (-:", name, gmx_version(), precisionString);
    const int indent =
            centeringOffset(78 - std::strlen(prefix) - std::strlen(suffix), title.length()) + 1;
    writer->writeLine(formatString("%s%*c%s%s", prefix, indent, ' ', title.c_str(), suffix));
    writer->writeLine(formatString("%s%s", prefix, suffix));
    const char* const binaryPath = programContext.fullBinaryPath();
    if (!gmx::isNullOrEmpty(binaryPath))
    {
        writer->writeLine(formatString("%sExecutable:   %s%s", prefix, binaryPath, suffix));
    }
    const gmx::InstallationPrefixInfo installPrefix = programContext.installationPrefix();
    if (!gmx::isNullOrEmpty(installPrefix.path))
    {
        writer->writeLine(formatString("%sData prefix:  %s%s%s",
                                       prefix,
                                       installPrefix.path,
                                       installPrefix.bSourceLayout ? " (source tree)" : "",
                                       suffix));
    }
    const std::string workingDir = Path::getWorkingDirectory();
    if (!workingDir.empty())
    {
        writer->writeLine(formatString("%sWorking dir:  %s%s", prefix, workingDir.c_str(), suffix));
    }
    if (settings.bProcessId_)
    {
        writer->writeLine(formatString("%sProcess ID:   %d%s", prefix, gmx_getpid(), suffix));
    }
    const char* const commandLine = programContext.commandLine();
    if (!gmx::isNullOrEmpty(commandLine))
    {
        writer->writeLine(formatString(
                "%sCommand line:%s\n%s  %s%s", prefix, suffix, prefix, commandLine, suffix));
    }
    if (settings.bExtendedInfo_)
    {
        GMX_RELEASE_ASSERT(prefix[0] == '\0' && suffix[0] == '\0',
                           "Prefix/suffix not supported with extended info");
        writer->ensureEmptyLine();
        gmx_print_version_info(writer);
    }
}

} // namespace gmx
