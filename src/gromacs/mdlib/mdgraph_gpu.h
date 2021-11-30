/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2022- The GROMACS Authors
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
 *
 * \brief Declares the MD Graph class
 *
 * \author Alan Gray <alang@nvidia.com>
 *
 *
 * \ingroup module_mdlib
 */
#ifndef GMX_MDRUN_MDGRAPH_H
#define GMX_MDRUN_MDGRAPH_H

#include "gromacs/mdtypes/simulation_workload.h"
#include "gromacs/timing/wallcycle.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/gmxmpi.h"

class GpuEventSynchronizer;

namespace gmx
{

class MdGpuGraph
{
public:
    /*! \brief Create MD graph object
     * \param [in] deviceStreamManager  Device stream manager object
     * \param [in] simulationWork       Simulation workload structure
     * \param [in] mpi_comm             MPI communicator for PP domain decomposition
     * \param [in] wcycle               Wall cycle timer object
     */
    MdGpuGraph(const DeviceStreamManager& deviceStreamManager,
               SimulationWorkload         simulationWork,
               MPI_Comm                   mpi_comm,
               gmx_wallcycle*             wcycle);

    ~MdGpuGraph();

    /*! \brief Denote start of graph region
     * \param [in] bNS                   Whether this is a search step
     * \param [in] canUseGraphThisStep   Whether graph can be used this step
     * \param [in] usedGraphLastStep     Whether graph was used in the last step
     * \param [in] xReadyOnDeviceEvent   Event marked when coordinates are ready on device
     */
    void start(bool bNS, bool canUseGraphThisStep, bool usedGraphLastStep, GpuEventSynchronizer* xReadyOnDeviceEvent);

    /*! \brief Denote end of graph region
     * \param [inout] xUpdatedOnDeviceEvent  Event marked when coordinates have been updated on device
     */
    void end(GpuEventSynchronizer* xUpdatedOnDeviceEvent);

    /*! \brief Whether graph is in use this step */
    bool useGraphThisStep() const;

    /*! \brief Whether graph is capturing */
    bool graphIsCapturing() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gmx
#endif
