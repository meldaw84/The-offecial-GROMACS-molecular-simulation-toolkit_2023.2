/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014,2015,2016, by the GROMACS development team.
 * Copyright (c) 2017,2018,2019,2020, by the GROMACS development team, led by
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
 *  \brief Declares functions to manage GPU resources.
 *
 *  This has several implementations: one for each supported GPU platform,
 *  and a stub implementation if the build does not support GPUs.
 *
 *  \author Anca Hamuraru <anca@streamcomputing.eu>
 *  \author Dimitrios Karkoulis <dimitris.karkoulis@gmail.com>
 *  \author Teemu Virolainen <teemu@streamcomputing.eu>
 *  \author Mark Abraham <mark.j.abraham@gmail.com>
 *  \author Szilárd Páll <pall.szilard@gmail.com>
 *  \author Artem Zhmurov <zhmurov@gmail.com>
 *
 * \inlibraryapi
 * \ingroup module_hardware
 */
#ifndef GMX_HARDWARE_DEVICE_MANAGEMENT_H
#define GMX_HARDWARE_DEVICE_MANAGEMENT_H

#include <memory>
#include <string>
#include <vector>

#include "gromacs/utility/basedefinitions.h"
#include "gromacs/utility/iserializer.h"

struct DeviceInformation;

/*! \brief Return whether GPUs can be detected.
 *
 * Returns true when this is a build of GROMACS configured to support
 * GPU usage, GPU detection is not disabled by \c GMX_DISABLE_GPU_DETECTION
 * environment variable and a valid device driver, ICD, and/or runtime was
 * detected. Does not throw.
 *
 * \param[out] errorMessage  When returning false on a build configured with
 *                           GPU support and non-nullptr was passed,
 *                           the string contains a descriptive message about
 *                           why GPUs cannot be detected.
 */
bool canPerformDeviceDetection(std::string* errorMessage);

/*! \brief Return whether GPU detection is enabled
 *
 * Returns true when this is a build of GROMACS configured to support
 * GPU usage and GPU detection is not disabled by \c GMX_DISABLE_GPU_DETECTION
 * environment variable.
 *
 * Does not throw.
 */
bool isDeviceDetectionEnabled();

/*! \brief Return whether GPU detection is functioning correctly
 *
 * Returns true when this is a build of GROMACS configured to support
 * GPU usage, and a valid device driver, ICD, and/or runtime was detected.
 *
 * This function is not intended to be called from build
 * configurations that do not support GPUs, and there will be no
 * descriptive message in that case.
 *
 * \param[out] errorMessage  When returning false on a build configured with
 *                           GPU support and non-nullptr was passed,
 *                           the string contains a descriptive message about
 *                           why GPUs cannot be detected.
 *
 * Does not throw.
 */
bool isDeviceDetectionFunctional(std::string* errorMessage);

/*! \brief Checks if one can compute on the GPU
 *
 * \returns  True if the build supports GPUs and there are at least one available.
 */
bool canComputeOnDevice();

/*! \brief Find all GPUs in the system.
 *
 *  Will detect every GPU supported by the device driver in use.
 *  Must only be called if \c canPerformDeviceDetection() has returned true.
 *  This routine also checks for the compatibility of each device and fill the
 *  deviceInfo array with the required information on each device: ID, device
 *  properties, status.
 *
 *  Note that this function leaves the GPU runtime API error state clean;
 *  this is implemented ATM in the CUDA flavor. This invalidates any existing
 *  CUDA streams, allocated memory on GPU, etc.
 *
 *  \todo:  Check if errors do propagate in OpenCL as they do in CUDA and
 *          whether there is a mechanism to "clear" them.
 *
 * \return  Standard vector with the list of devices found
 *
 *  \throws InternalError if a GPU API returns an unexpected failure (because
 *          the call to canDetectGpus() should always prevent this occuring)
 */
std::vector<std::unique_ptr<DeviceInformation>> findDevices();

/*! \brief Return a container of device-information handles that are compatible.
 *
 * This function filters the result of the detection for compatible
 * GPUs, based on the previously run compatibility tests.
 *
 * \param[in] deviceInfoList An information on available devices.
 *
 * \return  Vector of DeviceInformations on GPUs recorded as compatible
 */
std::vector<std::reference_wrapper<DeviceInformation>>
getCompatibleDevices(const std::vector<std::unique_ptr<DeviceInformation>>& deviceInfoList);

/*! \brief Return a container of the IDs of the compatible GPU ids.
 *
 * This function filters the result of the detection for compatible
 * GPUs, based on the previously run compatibility tests.
 *
 * \param[in] deviceInfoList An information on available devices.
 *
 * \return  Vector of compatible GPU ids.
 */
std::vector<int> getCompatibleDeviceIds(const std::vector<std::unique_ptr<DeviceInformation>>& deviceInfoList);

/*! \brief Return whether \c deviceId is found in \c deviceInfoList and is compatible
 *
 * This function filters the result of the detection for compatible
 * GPUs, based on the previously run compatibility tests.
 *
 * \param[in] deviceInfoList An information on available devices.
 * \param[in] deviceIId      An id of device to check.
 *
 * \throws RangeError If \c deviceId does not match the id of any device in \c deviceInfoList
 *
 * \return  Whether \c deviceId is compatible.
 */
bool deviceIdIsCompatible(const std::vector<std::unique_ptr<DeviceInformation>>& deviceInfoList,
                          int                                                    deviceId);

/*! \brief Set the active GPU.
 *
 * This sets the device for which the device information is passed active. Essential in CUDA, where
 * the device buffers and kernel launches are not connected to the device context. In OpenCL, checks
 * the device vendor and makes vendor-specific performance adjustments.
 *
 * \param[in] deviceInfo Information on the device to be set.
 *
 * Issues a fatal error for any critical errors that occur during
 * initialization.
 */
void setActiveDevice(const DeviceInformation& deviceInfo);

/*! \brief Releases the GPU device used by the active context at the time of calling (CUDA only).
 *
 * If \c deviceInfo is nullptr, then it is understood that no device
 * was selected so no context is active to be freed. Otherwise, the
 * context is explicitly destroyed and therefore all data uploaded to
 * the GPU is lost. This must only be called when none of this data is
 * required anymore, because subsequent attempts to free memory
 * associated with the context will otherwise fail.
 *
 * Calls \c gmx_warning upon errors.
 *
 * \todo This should go through all the devices, not only the one currently active.
 *       Reseting only one device will not work, e.g. in CUDA tests.
 *
 * \param[in] deviceInfo Information on the device to be released.
 */
void releaseDevice(DeviceInformation* deviceInfo);

/*! \brief Formats and returns a device information string for a given GPU.
 *
 * Given an index *directly* into the array of available GPUs, returns
 * a formatted info string for the respective GPU which includes ID, name,
 * compute capability, and detection status.
 *
 * \param[in] deviceInfo  An information on device that is to be set.
 *
 * \returns A string describing the device.
 */
std::string getDeviceInformationString(const DeviceInformation& deviceInfo);

/*! \brief Return a string describing how compatible the GPU with given \c deviceId is.
 *
 * \param[in] deviceInfoList An information on available devices.
 * \param[in] deviceId       An index of the device to check
 * \returns                  A string describing the compatibility status, useful for error messages.
 */
std::string getDeviceCompatibilityDescription(const std::vector<std::unique_ptr<DeviceInformation>>& deviceInfoList,
                                              int deviceId);

/*! \brief Serialization of information on devices for MPI broadcasting.
 *
 * \param[in] deviceInfoList  The vector with device informations to serialize.
 * \param[in] serializer      Serializing object.
 */
void serializeDeviceInformations(const std::vector<std::unique_ptr<DeviceInformation>>& deviceInfoList,
                                 gmx::ISerializer*                                      serializer);

/*! \brief Deserialization of information on devices after MPI broadcasting.
 *
 * \param[in] serializer Serializing object.
 *
 * \return deviceInfoList   Deserialized vector with device informations.
 */
std::vector<std::unique_ptr<DeviceInformation>> deserializeDeviceInformations(gmx::ISerializer* serializer);

#endif // GMX_HARDWARE_DEVICE_MANAGEMENT_H
