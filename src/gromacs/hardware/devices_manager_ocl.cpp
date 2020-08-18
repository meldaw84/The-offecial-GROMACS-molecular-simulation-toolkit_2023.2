/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014,2015,2017 The GROMACS development team.
 * Copyright (c) 2018,2019,2020, by the GROMACS development team, led by
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
/*! \libinternal \file
 *  \brief Defines the OpenCL implementations of the DevicesManager class.
 *
 *  \author Artem Zhmurov <zhmurov@gmail.com>
 *
 * \inlibraryapi
 * \ingroup module_hardware
 */
#include "gmxpre.h"

#include "config.h"

#include "gromacs/gpu_utils/oclraii.h"
#include "gromacs/gpu_utils/oclutils.h"
#include "gromacs/hardware/device_information.h"
#include "gromacs/hardware/devices_manager.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/utility/stringutil.h"

namespace gmx
{

/*! \brief Returns an DeviceVendor value corresponding to the input OpenCL vendor name.
 *
 *  \param[in] vendorName  String with OpenCL vendor name.
 *  \returns               DeviceVendor value for the input vendor name
 */
static DeviceVendor getDeviceVendor(const char* vendorName)
{
    if (vendorName)
    {
        if (strstr(vendorName, "NVIDIA"))
        {
            return DeviceVendor::Nvidia;
        }
        else if (strstr(vendorName, "AMD") || strstr(vendorName, "Advanced Micro Devices"))
        {
            return DeviceVendor::Amd;
        }
        else if (strstr(vendorName, "Intel"))
        {
            return DeviceVendor::Intel;
        }
    }
    return DeviceVendor::Unknown;
}

/*! \brief Return true if executing on compatible OS for AMD OpenCL.
 *
 * This is assumed to be true for OS X version of at least 10.10.4 and
 * all other OS flavors.
 *
 * Uses the BSD sysctl() interfaces to extract the kernel version.
 *
 * \return true if version is 14.4 or later (= OS X version 10.10.4),
 *         or OS is not Darwin.
 */
static bool runningOnCompatibleOSForAmd()
{
#ifdef __APPLE__
    int    mib[2];
    char   kernelVersion[256];
    size_t len = sizeof(kernelVersion);

    mib[0] = CTL_KERN;
    mib[1] = KERN_OSRELEASE;

    sysctl(mib, sizeof(mib) / sizeof(mib[0]), kernelVersion, &len, NULL, 0);

    int major = strtod(kernelVersion, NULL);
    int minor = strtod(strchr(kernelVersion, '.') + 1, NULL);

    // Kernel 14.4 corresponds to OS X 10.10.4
    return (major > 14 || (major == 14 && minor >= 4));
#else
    return true;
#endif
}

/*!
 * \brief Checks that device \c deviceInfo is compatible with GROMACS.
 *
 *  Vendor and OpenCL version support checks are executed an the result
 *  of these returned.
 *
 * \param[in]  deviceInfo  The device info pointer.
 * \returns                The result of the compatibility checks.
 */
static DeviceStatus isDeviceSupported(const DeviceInformation& deviceInfo)
{
    if (getenv("GMX_OCL_DISABLE_COMPATIBILITY_CHECK") != nullptr)
    {
        // Assume the device is compatible because checking has been disabled.
        return DeviceStatus::Compatible;
    }

    // OpenCL device version check, ensure >= REQUIRED_OPENCL_MIN_VERSION
    constexpr unsigned int minVersionMajor = REQUIRED_OPENCL_MIN_VERSION_MAJOR;
    constexpr unsigned int minVersionMinor = REQUIRED_OPENCL_MIN_VERSION_MINOR;

    // Based on the OpenCL spec we're checking the version supported by
    // the device which has the following format:
    //      OpenCL<space><major_version.minor_version><space><vendor-specific information>
    unsigned int deviceVersionMinor, deviceVersionMajor;
    const int    valuesScanned = std::sscanf(deviceInfo.device_version, "OpenCL %u.%u",
                                          &deviceVersionMajor, &deviceVersionMinor);
    const bool   versionLargeEnough =
            ((valuesScanned == 2)
             && ((deviceVersionMajor > minVersionMajor)
                 || (deviceVersionMajor == minVersionMajor && deviceVersionMinor >= minVersionMinor)));
    if (!versionLargeEnough)
    {
        return DeviceStatus::Incompatible;
    }

    /* Only AMD, Intel, and NVIDIA GPUs are supported for now */
    switch (deviceInfo.deviceVendor)
    {
        case DeviceVendor::Nvidia: return DeviceStatus::Compatible;
        case DeviceVendor::Amd:
            return runningOnCompatibleOSForAmd() ? DeviceStatus::Compatible : DeviceStatus::Incompatible;
        case DeviceVendor::Intel:
            return GMX_OPENCL_NB_CLUSTER_SIZE == 4 ? DeviceStatus::Compatible
                                                   : DeviceStatus::IncompatibleClusterSize;
        default: return DeviceStatus::Incompatible;
    }
}

/*!
 * \brief Checks that device \c deviceInfo is sane (ie can run a kernel).
 *
 * Compiles and runs a dummy kernel to determine whether the given
 * OpenCL device functions properly.
 *
 *
 * \param[in]  deviceInfo      The device info pointer.
 * \param[out] errorMessage    An error message related to a failing OpenCL API call.
 * \throws     std::bad_alloc  When out of memory.
 * \returns                    Whether the device passed sanity checks
 */
static bool isDeviceFunctional(const DeviceInformation& deviceInfo, std::string* errorMessage)
{
    cl_context_properties properties[] = {
        CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(deviceInfo.oclPlatformId), 0
    };
    // uncrustify spacing

    cl_int    status;
    auto      deviceId = deviceInfo.oclDeviceId;
    ClContext context(clCreateContext(properties, 1, &deviceId, nullptr, nullptr, &status));
    if (status != CL_SUCCESS)
    {
        errorMessage->assign(makeOpenClInternalErrorString("clCreateContext", status));
        return false;
    }
    ClCommandQueue commandQueue(clCreateCommandQueue(context, deviceId, 0, &status));
    if (status != CL_SUCCESS)
    {
        errorMessage->assign(makeOpenClInternalErrorString("clCreateCommandQueue", status));
        return false;
    }

    // Some compilers such as Apple's require kernel functions to have at least one argument
    const char* lines[] = { "__kernel void dummyKernel(__global void* input){}" };
    ClProgram   program(clCreateProgramWithSource(context, 1, lines, nullptr, &status));
    if (status != CL_SUCCESS)
    {
        errorMessage->assign(makeOpenClInternalErrorString("clCreateProgramWithSource", status));
        return false;
    }

    if ((status = clBuildProgram(program, 0, nullptr, nullptr, nullptr, nullptr)) != CL_SUCCESS)
    {
        errorMessage->assign(makeOpenClInternalErrorString("clBuildProgram", status));
        return false;
    }

    ClKernel kernel(clCreateKernel(program, "dummyKernel", &status));
    if (status != CL_SUCCESS)
    {
        errorMessage->assign(makeOpenClInternalErrorString("clCreateKernel", status));
        return false;
    }

    clSetKernelArg(kernel, 0, sizeof(void*), nullptr);

    const size_t localWorkSize = 1, globalWorkSize = 1;
    if ((status = clEnqueueNDRangeKernel(commandQueue, kernel, 1, nullptr, &globalWorkSize,
                                         &localWorkSize, 0, nullptr, nullptr))
        != CL_SUCCESS)
    {
        errorMessage->assign(makeOpenClInternalErrorString("clEnqueueNDRangeKernel", status));
        return false;
    }
    return true;
}

/*! \brief Check whether the \c ocl_gpu_device is suitable for use by mdrun
 *
 * Runs sanity checks: checking that the runtime can compile a dummy kernel
 * and this can be executed;
 * Runs compatibility checks verifying the device OpenCL version requirement
 * and vendor/OS support.
 *
 * \param[in]  deviceId      The runtime-reported numeric ID of the device.
 * \param[in]  deviceInfo    The device info pointer.
 * \returns  A DeviceStatus to indicate if the GPU device is supported and if it was able to run
 *           basic functionality checks.
 */
static DeviceStatus checkGpu(size_t deviceId, const DeviceInformation& deviceInfo)
{

    DeviceStatus supportStatus = isDeviceSupported(deviceInfo);
    if (supportStatus != DeviceStatus::Compatible)
    {
        return supportStatus;
    }

    std::string errorMessage;
    if (!isDeviceFunctional(deviceInfo, &errorMessage))
    {
        gmx_warning("While sanity checking device #%zu, %s", deviceId, errorMessage.c_str());
        return DeviceStatus::NonFunctional;
    }

    return DeviceStatus::Compatible;
}

} // namespace gmx

DevicesManager::~DevicesManager() = default;

void DevicesManager::freeDevice(DeviceInformation* /* deviceInfo */) {}

bool DevicesManager::isGpuDetectionFunctional(std::string* errorMessage)
{
    cl_uint numPlatforms;
    cl_int  status = clGetPlatformIDs(0, nullptr, &numPlatforms);
    GMX_ASSERT(status != CL_INVALID_VALUE, "Incorrect call of clGetPlatformIDs detected");
#ifdef cl_khr_icd
    if (status == CL_PLATFORM_NOT_FOUND_KHR)
    {
        // No valid ICDs found
        if (errorMessage != nullptr)
        {
            errorMessage->assign("No valid OpenCL driver found");
        }
        return false;
    }
#endif
    GMX_RELEASE_ASSERT(
            status == CL_SUCCESS,
            gmx::formatString("An unexpected value was returned from clGetPlatformIDs %d: %s",
                              status, ocl_get_error_string(status).c_str())
                    .c_str());
    bool foundPlatform = (numPlatforms > 0);
    if (!foundPlatform && errorMessage != nullptr)
    {
        errorMessage->assign("No OpenCL platforms found even though the driver was valid");
    }
    return foundPlatform;
}

std::vector<std::unique_ptr<DeviceInformation>> DevicesManager::findDevices()
{
    cl_uint         ocl_platform_count;
    cl_platform_id* ocl_platform_ids;
    cl_device_type  req_dev_type = CL_DEVICE_TYPE_GPU;

    ocl_platform_ids = nullptr;

    if (getenv("GMX_OCL_FORCE_CPU") != nullptr)
    {
        req_dev_type = CL_DEVICE_TYPE_CPU;
    }

    int                                             numDevices = 0;
    std::vector<std::unique_ptr<DeviceInformation>> deviceInfos(0);

    while (true)
    {
        cl_int status = clGetPlatformIDs(0, nullptr, &ocl_platform_count);
        if (CL_SUCCESS != status)
        {
            GMX_THROW(gmx::InternalError(
                    gmx::formatString("An unexpected value %d was returned from clGetPlatformIDs: ", status)
                    + ocl_get_error_string(status)));
        }

        if (1 > ocl_platform_count)
        {
            // TODO this should have a descriptive error message that we only support one OpenCL platform
            break;
        }

        snew(ocl_platform_ids, ocl_platform_count);

        status = clGetPlatformIDs(ocl_platform_count, ocl_platform_ids, nullptr);
        if (CL_SUCCESS != status)
        {
            GMX_THROW(gmx::InternalError(
                    gmx::formatString("An unexpected value %d was returned from clGetPlatformIDs: ", status)
                    + ocl_get_error_string(status)));
        }

        for (unsigned int i = 0; i < ocl_platform_count; i++)
        {
            cl_uint ocl_device_count;

            /* If requesting req_dev_type devices fails, just go to the next platform */
            if (CL_SUCCESS != clGetDeviceIDs(ocl_platform_ids[i], req_dev_type, 0, nullptr, &ocl_device_count))
            {
                continue;
            }

            if (1 <= ocl_device_count)
            {
                numDevices += ocl_device_count;
            }
        }

        if (1 > numDevices)
        {
            break;
        }

        deviceInfos.resize(numDevices);

        {
            int           device_index;
            cl_device_id* ocl_device_ids;

            snew(ocl_device_ids, numDevices);
            device_index = 0;

            for (unsigned int i = 0; i < ocl_platform_count; i++)
            {
                cl_uint ocl_device_count;

                /* If requesting req_dev_type devices fails, just go to the next platform */
                if (CL_SUCCESS
                    != clGetDeviceIDs(ocl_platform_ids[i], req_dev_type, numDevices, ocl_device_ids,
                                      &ocl_device_count))
                {
                    continue;
                }

                if (1 > ocl_device_count)
                {
                    break;
                }

                for (unsigned int j = 0; j < ocl_device_count; j++)
                {
                    deviceInfos[device_index] = std::make_unique<DeviceInformation>();

                    deviceInfos[device_index]->id = device_index;

                    deviceInfos[device_index]->oclPlatformId = ocl_platform_ids[i];
                    deviceInfos[device_index]->oclDeviceId   = ocl_device_ids[j];

                    deviceInfos[device_index]->device_name[0] = 0;
                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_NAME,
                                    sizeof(deviceInfos[device_index]->device_name),
                                    deviceInfos[device_index]->device_name, nullptr);

                    deviceInfos[device_index]->device_version[0] = 0;
                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_VERSION,
                                    sizeof(deviceInfos[device_index]->device_version),
                                    deviceInfos[device_index]->device_version, nullptr);

                    deviceInfos[device_index]->vendorName[0] = 0;
                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_VENDOR,
                                    sizeof(deviceInfos[device_index]->vendorName),
                                    deviceInfos[device_index]->vendorName, nullptr);

                    deviceInfos[device_index]->compute_units = 0;
                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_MAX_COMPUTE_UNITS,
                                    sizeof(deviceInfos[device_index]->compute_units),
                                    &(deviceInfos[device_index]->compute_units), nullptr);

                    deviceInfos[device_index]->adress_bits = 0;
                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_ADDRESS_BITS,
                                    sizeof(deviceInfos[device_index]->adress_bits),
                                    &(deviceInfos[device_index]->adress_bits), nullptr);

                    deviceInfos[device_index]->deviceVendor =
                            gmx::getDeviceVendor(deviceInfos[device_index]->vendorName);

                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_MAX_WORK_ITEM_SIZES, 3 * sizeof(size_t),
                                    &deviceInfos[device_index]->maxWorkItemSizes, nullptr);

                    clGetDeviceInfo(ocl_device_ids[j], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t),
                                    &deviceInfos[device_index]->maxWorkGroupSize, nullptr);

                    deviceInfos[device_index]->status =
                            gmx::checkGpu(device_index, *deviceInfos[device_index]);

                    device_index++;
                }
            }

            numDevices = device_index;

            /* Dummy sort of devices -  AMD first, then NVIDIA, then Intel */
            // TODO: Sort devices based on performance.
            if (0 < numDevices)
            {
                int last = -1;
                for (int i = 0; i < numDevices; i++)
                {
                    if (deviceInfos[i]->deviceVendor == DeviceVendor::Amd)
                    {
                        last++;

                        if (last < i)
                        {
                            std::swap(deviceInfos[i], deviceInfos[last]);
                        }
                    }
                }

                /* if more than 1 device left to be sorted */
                if ((numDevices - 1 - last) > 1)
                {
                    for (int i = 0; i < numDevices; i++)
                    {
                        if (deviceInfos[i]->deviceVendor == DeviceVendor::Nvidia)
                        {
                            last++;

                            if (last < i)
                            {
                                std::swap(deviceInfos[i], deviceInfos[last]);
                            }
                        }
                    }
                }
            }

            sfree(ocl_device_ids);
        }

        break;
    }

    sfree(ocl_platform_ids);
    return deviceInfos;
}

void DevicesManager::setDevice(const DeviceInformation& deviceInfo)
{
    int deviceId = deviceInfo.id;

    // If the device is NVIDIA, for safety reasons we disable the JIT
    // caching as this is known to be broken at least until driver 364.19;
    // the cache does not always get regenerated when the source code changes,
    // e.g. if the path to the kernel sources remains the same

    if (deviceInfo.deviceVendor == DeviceVendor::Nvidia)
    {
        // Ignore return values, failing to set the variable does not mean
        // that something will go wrong later.
#ifdef _MSC_VER
        _putenv("CUDA_CACHE_DISABLE=1");
#else
        // Don't override, maybe a dev is testing.
        setenv("CUDA_CACHE_DISABLE", "1", 0);
#endif
    }
}

std::string DevicesManager::getDeviceInformationString(const DeviceInformation& deviceInfo)
{
    bool gpuExists = (deviceInfo.status != DeviceStatus::Nonexistent
                      && deviceInfo.status != DeviceStatus::NonFunctional);

    if (!gpuExists)
    {
        return gmx::formatString("#%d: %s, status: %s", deviceInfo.id, "N/A",
                                 c_deviceStateString[deviceInfo.status]);
    }
    else
    {
        return gmx::formatString("#%d: name: %s, vendor: %s, device version: %s, status: %s",
                                 deviceInfo.id, deviceInfo.device_name, deviceInfo.vendorName,
                                 deviceInfo.device_version, c_deviceStateString[deviceInfo.status]);
    }
}

size_t DevicesManager::getDeviceInformationSize()
{
    return sizeof(DeviceInformation);
}
