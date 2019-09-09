/*
 * This source file is part of the Alexandria program.
 *
 * Copyright (C) 2014-2019 
 *
 * Developers:
 *             Mohammad Mehdi Ghahremanpour, 
 *             Paul J. van Maaren, 
 *             David van der Spoel (Project leader)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */
 
/*! \internal \brief
 * Implements part of the alexandria program.
 * \author Mohammad Mehdi Ghahremanpour <mohammad.ghahremanpour@icm.uu.se>
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 */

#ifndef ALEXANDRIA_OPTPARAM_H
#define ALEXANDRIA_OPTPARAM_H

#include <functional>
#include <random>
#include <vector>

#include "gromacs/commandline/pargs.h"
#include "gromacs/fileio/oenv.h"
#include "gromacs/fileio/xvgr.h"
#include "gromacs/math/functions.h"
#include "gromacs/math/units.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/real.h"

namespace alexandria
{

/*! \brief
 * Does Bayesian Monte Carlo (BMC) simulation to find the best paramater set,
 * which has the lowest chi-squared.
 *
 * \inpublicapi
 * \ingroup module_alexandria
 */


class OptParam
{
    private:
        int                     maxiter_;
        const gmx_output_env_t *oenv_;
        const char             *xvgconv_;
        const char             *xvgepot_;
        gmx_bool                bBound_;
        real                    seed_;
        real                    step_;
        real                    temperature_;
        bool                    anneal_;
        
    public:

        OptParam() 
            : 
                maxiter_(100), 
                oenv_(nullptr), 
                xvgconv_(nullptr), 
                xvgepot_(nullptr), 
                bBound_(false), 
                seed_(-1), 
                step_(0.02), 
                temperature_(5), 
                anneal_(true)
            {}

        ~OptParam() {};

        /*! \brief Add command line arguments
         *
         * \param[in] pargs Vector of pargs
         */
        void add_pargs(std::vector<t_pargs> *pargs);

        /*! \brief
         * Set the output file names
         * \param[in] xvgconv The parameter convergence
         * \param[in] xvgepot The chi2 value
         * \param[in] oenv    GROMACS utility structure
         */
        void setOutputFiles(const char             *xvgconv,
                            const char             *xvgepot,
                            const gmx_output_env_t *oenv);

        /*! \brief Compute and return the Boltzmann factor
         *
         * \param[in] iter  The iteration number
         * \return The Boltzmann factor
         */
        double computeBeta(int iter);
        
        /*! \brief Compute and return the Boltzmann factor
         * it applies periodic annealing
         *
         * \param[in] maxiter The maximum number of itearion
         * \param[in] iter    The iteration number
         * \param[in] ncycle  The multiplicity of the cosine function
         * \return The Boltzmann factor
         */
        double computeBeta(int maxiter, int iter, int ncycle);

        //! \brief Return Max # iterations
        int maxIter() const { return maxiter_; }

        //! \brief Return the step
        real step() const { return step_; }
        
        //! \brief Addapt the step size for perturbing the parameter
        void adapt_step(real factor) {step_ *= factor ;}

        //! \brief Use box constraints or not
        void setBounds(bool bBound) { bBound_ = bBound; }

        //! \brief Return whether or not bounds are used for parameters
        bool bounds() const { return bBound_; }

        //! \brief Return xvg file for convergence information
        const char *xvgConv() const { return xvgconv_; }

        //! \brief Return xvg file for epot information
        const char *xvgEpot() const { return xvgepot_; }

        //! \brief Return output environment
        const gmx_output_env_t *oenv() const { return oenv_; }
};

class Bayes : public OptParam
{
    using func_t = std::function<double (double v[])>;
    using parm_t = std::vector<double>;

    private:
        func_t  func_;
        parm_t  param_;
        parm_t  psigma_;
        parm_t  pmean_;
        parm_t  lowerBound_;
        parm_t  upperBound_;
        parm_t  bestParam_;
        double *minEval_;

    public:

        Bayes() {}

        void setFunc(func_t func_, 
                     double *minEval_);

        /*! \brief 
         * Finalizes the parameter setup, that means this should be
         * called after the last "addParam" call.
         * Routine will copy the current parameters to the
         * best parameters.
         * Set the bounds for the optimization between 
         * 1/factor and factor times the starting value.
         * Will fail an assertion when factor <= 0
         * \param[in] factor The scaling factor
         */
        void setParamBounds(real factor);
        
        void setParam(parm_t param);

        /*! \brief
         * Change parameter j based on a random unmber
         * obtained from a uniform distribution.
         */
        void changeParam(int j, real rand);

        //! \brief Return the number of parameters
        size_t nParam() const { return param_.size(); }

        /*! \brief
         * Dump the current parameters to a FILE if not nullptr
         * \param[in] fp The file pointer
         */
        void dumpParam(FILE *fp);        
        /*! \brief
         * Append parameter and set it to value
         * \param[val] The value
         */
        void addParam(real val)
        {
            param_.push_back(val);
        }

        /*! \brief
         * Set parameter j to a new value
         * \param[j]   Index
         * \param[val] The new value
         */
        void setParam(size_t j, real val)
        {
            GMX_RELEASE_ASSERT(j < param_.size(), "Parameter out of range");
            param_[j] = val;
        }

        /*! \brief
         * Returns the current vector of parameters.
         */
        const parm_t &getParam() const { return param_; }

        /*! \brief
         * Returns the vector of best found value for each parameter.
         */
        const parm_t &getBestParam() const { return bestParam_; }

        /*! \brief
         * Returns the vector of mean value calculated for each parameter.
         */
        const parm_t &getPmean() const { return pmean_; }

        /*! \brief
         * Returns the vector of standard deviation calculated for each parameter.
         */
        const parm_t &getPsigma() const { return psigma_; };

        /*! \brief
         * Run the Markov chain Monte carlo (MCMC) simulation
         *
         */
        void MCMC();
        
        /*! \brief
         * Run the Delayed Rejected Adaptive Monte-Carlo (DRAM) simulation
         *
         */
        void DRAM();

        /*! \brief
         * Copy the optimization parameters to the poldata structure
         * \param[in] List over the parameters that have changed.
         */
        virtual void toPolData(const std::vector<bool> &changed) = 0;

        //! Compute the chi2 from the target function
        virtual double calcDeviation() = 0;

        /*! \brief
         * Objective function for parameter optimization
         * \param[in] v Array of parameters.
         * \return Total value (chi2) corresponding to deviation
         */
        double objFunction(const double v[]);
};

}

#endif
