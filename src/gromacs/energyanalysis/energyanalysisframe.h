/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2019- The GROMACS Authors
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
/*! \libinternal \file
 * \brief
 * Declares gmx::EnergyAnalysisFrame
 *
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 * \ingroup module_energyanalysis
 */
#ifndef GMX_ENERGYANALYSIS_ENERGYANALYSISFRAME_H
#define GMX_ENERGYANALYSIS_ENERGYANALYSISFRAME_H

#include <string>

#include "gromacs/utility/basedefinitions.h"

namespace gmx
{

/*! \libinternal
 * \brief
 * Class describing an energy frame, that is the all the data
 * stored for one energy term at one time step in an energy file.
 * \todo Make this supersede t_enxframe.
 */
class EnergyAnalysisFrame
{
public:
    /*! \brief
     * Constructor
     * \param[in] t The time
     * \param[in] step The MD step
     * \param[in] e The instantaneous energy
     * \param[in] nsum The number of statistics points
     * \param[in] esum The sum of energies over nsum_ previous steps
     * \param[in] evar The variance over nsum_ previous steps
     */
    EnergyAnalysisFrame(double t, int64_t step, double e, int nsum, double esum, double evar) :
        t_(t),
        step_(step),
        e_(e),
        nsum_(nsum),
        esum_(esum),
        evar_(evar)
    {
    }
    /*! \brief Return the time
     *
     * \return The time
     */
    double time() const { return t_; }
    /*! \brief Return the step
     *
     * \return the step
     */
    int64_t step() const { return step_; }
    /*! \brief Return the instantaneous energy
     *
     * \return the instantaneous energy
     */
    double energy() const { return e_; }
    /*! \brief Return the number of statistics points
     *
     * \return the number of statistics points
     */
    int nSum() const { return nsum_; }
    /*! \brief Return the sum of energies
     *
     * \return the sum of energies
     */
    double energySum() const { return esum_; }
    /*! \brief Return the variance of energies
     *
     * \return the variance of energies
     */
    double variance() const { return evar_; }

private:
    //! The time in the simulation
    double t_;
    //! Thw step in the simulation
    int64_t step_;
    //! The energy at this time point
    double e_;
    //! The number of statistics points
    int nsum_;
    //! The sum of energies over nsum_ previous steps
    double esum_;
    //! The variance over nsum_ previous steps
    double evar_;
};

} // namespace gmx
#endif
