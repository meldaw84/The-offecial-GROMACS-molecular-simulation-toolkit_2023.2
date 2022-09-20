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
 * \brief
 * Declares empy force provider for lambda dynamics
 *
 * \author Pavel Buslaev <pavel.i.buslaev@jyu.fi>
 * \ingroup module_applied_forces
 */
#ifndef GMX_APPLIED_FORCES_LAMBDADYNAMICSFORCEPROVIDER_H
#define GMX_APPLIED_FORCES_LAMBDADYNAMICSFORCEPROVIDER_H

#include "gromacs/mdtypes/forceoutput.h"
#include "gromacs/mdtypes/iforceprovider.h"
#include "gromacs/pbcutil/pbc.h"
#include "gromacs/utility/classhelpers.h"
#include "gromacs/utility/logger.h"


namespace gmx
{

#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wunused-private-field"
#endif

//! Type for CP2K force environment handle
typedef int force_env_t;

/*! \internal \brief
 * Implements IForceProvider for QM/MM.
 */
class LambdaDynamicsForceProvider final : public IForceProvider
{
public:
    LambdaDynamicsForceProvider(PbcType               pbcType,
                                const MDLogger&       logger);

    //! Destruct force provider for QMMM and finalize libcp2k
    ~LambdaDynamicsForceProvider() {};

    /*!\brief Calculate forces of QMMM.
     * \param[in] fInput input for force provider
     * \param[out] fOutput output for force provider
     */
    void calculateForces(const ForceProviderInput& fInput, ForceProviderOutput* fOutput) override;

private:
    //! Write message to the log
    void appendLog(const std::string& msg);

    const PbcType         pbcType_;
    const MDLogger&       logger_;
};

#ifdef __clang__
#    pragma clang diagnostic pop
#endif

} // namespace gmx

#endif // GMX_APPLIED_FORCES_LAMBDADYNAMICSFORCEPROVIDER_H
