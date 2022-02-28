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
 * Declares gmx::EnergyTerm
 *
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 * \ingroup module_energyanalysis
 */
#ifndef GMX_ENERGYANALYSIS_ENERGYTERM_H
#define GMX_ENERGYANALYSIS_ENERGYTERM_H

#include <string>
#include <vector>

#include "gromacs/utility/basedefinitions.h"
#include "gromacs/utility/real.h"

#include "energyanalysisframe.h"

namespace gmx
{

//! Typedef for looping over EnergyFrame
using EnergyAnalysisFrameIterator = std::vector<EnergyAnalysisFrame>::const_iterator;

/*! \libinternal
 * \brief
 * Class describing the whole time series of an energy term.
 */
class EnergyTerm
{
private:
    //! Name of the energy term
    std::string eTerm_;
    //! Unit of this energy
    std::string eUnit_;
    //! Number of energy terms summed so far
    int64_t nesum_;
    //! First MD step in the analysis
    int64_t step0_;
    //! Last MD step in the analysis
    int64_t step1_;
    //! Index in the energy array in the energy file
    unsigned int findex_;
    //! Best estimate of the average energy so far
    double energy_;
    //! Best estimate of the standard deviation so far
    double stddev_;
    //! Start time of the analysis
    double t0_;
    //! End time of the analysis
    double t1_;
    //! Boolean whether firstframe has been read
    bool firstFrameRead_;
    //! Boolean indicating whether we are storing data in the vectors below
    bool storeData_;
    //! Array of energy frames
    std::vector<EnergyAnalysisFrame> ef_;
    //! Total sum of energy
    double esumTot_;
    //! Total variance of energy
    double evarTot_;
    //! Is the present energy term really an energy?
    bool isEner_;

public:
    /*! \brief
     * Constructor
     * \param[in] findex File index (in the energies stored)
     * \param[in] bStoreData boolean indicating whether to store the data
     * \param[in] eTerm  String describing the energy
     * \param[in] eUnit  String describing the energy unit
     */
    EnergyTerm(unsigned int findex, bool bStoreData, const std::string& eTerm, const std::string& eUnit);

    //! Return the index in the file to the function type stored here
    unsigned int fileIndex() const { return findex_; }

    //! Return the name corresponding to the energy term
    std::string name() const { return eTerm_; }

    //! Return the name corresponding to the energy unit
    std::string unit() const { return eUnit_; }

    /*! \brief
     * Tell the class to store or not to store data
     * \param[in] bStoreData Boolean
     */
    void setStoreData(bool bStoreData) { storeData_ = bStoreData; }

    //! Return the store data variable
    bool storeData() const { return storeData_; }

    //! Is this a true energy or e.g. Temperature
    bool isEner() const { return isEner_; }

    //! Return iterator to begin looping over energy frames
    EnergyAnalysisFrameIterator begin() const { return ef_.begin(); }

    //! Return iterator to end looping over energy frames
    EnergyAnalysisFrameIterator end() const { return ef_.end(); }

    /*! \brief
     * Return the energy frame corresponding to a certain step
     * \param[in] iframe The index in the array
     * \return the actual EnergyFrameIterator, or end() if not found
     */
    EnergyAnalysisFrameIterator findFrame(int64_t iframe) const;

    /*! \brief
     * Add a data frame to this EnergyTerm
     * \param[in] t    The time in the simulation
     * \param[in] step The simulation step
     * \param[in] nsum The number of intermediate steps for the sums
     * \param[in] esum The sum of energies over the last nsum steps
     * \param[in] evar The variance of the energies over the last nsum steps
     * \param[in] e    The energy at this point in time (trajectory)
     */
    void addFrame(double t, int64_t step, int nsum, double esum, double evar, double e);

    //! Return the average energy
    double average() const { return energy_; }

    //! Return the standard deviation
    double standardDeviation() const { return stddev_; }

    /*! \brief
     * Compute an error estimate based on block averaging.
     * Requires that the energies have been stored.
     * \param[in] nb Number of blocks
     * \param[out] ee the error estimate
     * \return true if an error estimate was computed
     */
    bool errorEstimate(unsigned int nb, real* ee) const;

    /*! \brief
     * Calculate the drift - can only be done when the data is stored.
     * This is done by fitting the data to a line y = ax + b.
     * \param[out] drift The slope of the line (property per time unit)
     * \return true if the result was indeed calculated
     */
    bool drift(real* drift) const;

    //! Return the number of points stored
    int64_t numFrames() const { return ef_.size(); }

    //! Return the length of the data set in time
    double timeSpan() const { return timeEnd() - timeBegin(); }

    //! Return the begin time of the data set
    double timeBegin() const { return t0_; }

    //! Return the end time of the data set
    double timeEnd() const { return t1_; }

    //! Return the length of the data set in time
    int64_t numSteps() const { return 1 + (stepEnd() - stepBegin()); }

    //! Return the begin time of the data set
    int64_t stepBegin() const { return step0_; }

    //! Return the end time of the data set
    int64_t stepEnd() const { return step1_; }
};

} // namespace gmx
#endif
