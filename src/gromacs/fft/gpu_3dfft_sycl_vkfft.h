/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2021- The GROMACS Authors
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
 *  \brief Declares the GPU 3D FFT routines.
 *
 *  \author Aleksei Iupinov <a.yupinov@gmail.com>
 *  \author Mark Abraham <mark.j.abraham@gmail.com>
 *  \author Gaurav Garg <gaugarg@nvidia.com>
 *  \ingroup module_fft
 */

#ifndef GMX_FFT_GPU_3DFFT_VKFFT_HPP
#define GMX_FFT_GPU_3DFFT_VKFFT_HPP

#include <memory>

#include "gromacs/fft/fft.h"
#include "gromacs/gpu_utils/devicebuffer_datatype.h"
#include "gromacs/gpu_utils/gputraits.h"
#include "gromacs/utility/gmxmpi.h"

#include "gpu_3dfft_impl.h"


#define VKFFT_BACKEND 2
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include "../external/vkfft/vkFFT.h"
#pragma clang diagnostic pop


class DeviceContext;
class DeviceStream;

namespace gmx
{

/*! \internal \brief
 * A 3D FFT wrapper class for performing R2C/C2R transforms using VkFFT
 */
class Gpu3dFft::ImplSyclVkfft : public Gpu3dFft::Impl
{
public:
    //! \copydoc Gpu3dFft::Impl::Impl
    ImplSyclVkfft(bool                 allocateGrids,
                  MPI_Comm             comm,
                  ArrayRef<const int>  gridSizesInXForEachRank,
                  ArrayRef<const int>  gridSizesInYForEachRank,
                  int                  nz,
                  bool                 performOutOfPlaceFFT,
                  const DeviceContext& context,
                  const DeviceStream&  pmeStream,
                  ivec                 realGridSize,
                  ivec                 realGridSizePadded,
                  ivec                 complexGridSizePadded,
                  DeviceBuffer<float>* realGrid,
                  DeviceBuffer<float>* complexGrid);

    //! \copydoc Gpu3dFft::Impl::~Impl
    ~ImplSyclVkfft() override;

    //! \copydoc Gpu3dFft::Impl::perform3dFft
    void perform3dFft(gmx_fft_direction dir, CommandEvent* timingEvent) override;

private:
    VkFFTConfiguration configuration;
    VkFFTApplication   appR2C;
    VkFFTLaunchParams  launchParams;
    uint64_t           bufferSize;
    uint64_t           inputBufferSize;
    hipDevice_t        queue_device;

    DeviceBuffer<float> realGrid_;

    hipStream_t backend_stream;
    sycl::queue queue_;
};

} // namespace gmx

#endif // GMX_FFT_GPU_3DFFT_VKFFT_HPP
