/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2019, by the GROMACS development team, led by
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
#include "gmxpre.h"

#include "energyanalysisrunner.h"

#include <cstdlib>

#include "gromacs/options.h"
#include "gromacs/analysisdata/modules/plot.h"
#include "gromacs/commandline/cmdlineoptionsmodule.h"
#include "gromacs/commandline/cmdlineparser.h"
#include "gromacs/commandline/cmdlineprogramcontext.h"
#include "gromacs/fileio/enxio.h"
#include "gromacs/fileio/oenv.h"
#include "gromacs/fileio/trxio.h"
#include "gromacs/options/timeunitmanager.h"
#include "gromacs/trajectory/energyframe.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/unique_cptr.h"

namespace gmx
{

namespace
{

/*! \brief
 * Class doing the actual reading of an energy file.
 *
 * Reading of the file (and hence dealing with underlying file formats)
 * is concentrated in this class and results are passed on to the
 * different energy analysis tools.
 */
class RunnerModule : public ICommandLineOptionsModule
{
    public:
        /*! \brief
         * Initiate local variables and register the analysis module
         *
         * \param[in] module Pointer to the actual analysis to be performed
         */
        RunnerModule(IEnergyAnalysisPointer module);

        void init(CommandLineModuleSettings * /*settings*/) override {}

        void initOptions(IOptionsContainer                 *options,
                         ICommandLineOptionsModuleSettings *settings) override;
        /*! \brief
         * Called when the last options have been processed
         */
        void optionsFinished() override {}

        /*! \brief
         * Read the files and call the tools to analyze them.
         *
         * The files are read
         * sequentially and tools have to be able to deal with this.
         * \return 0 if everything went smoothly
         */
        int run() override;
    private:
        //! Check whether time is within range
        int checkTime(double t);

        //! Initiate the module with energy terms
        void doInitModule(ener_file_t fp);

        //! The energy file
        std::string                              fnEnergy_;
        //! Start time of the analysis
        double                                   t0_;
        //! End time of the analysis
        double                                   t1_;
        //! Skipping time of the analysis
        double                                   tDelta_;
        //! Do we want to view the output?
        bool                                     bView_;
        //! Do we want verbose output?
        bool                                     bVerbose_;
        //! Module that does all the work
        IEnergyAnalysisPointer                   module_;
        //! Global plotting settings for the analysis module.
        AnalysisDataPlotSettings                 plotSettings_;
        //! Global time unit setting for the analysis module.
        TimeUnit                                 timeUnit_;
        //! Output environment
        gmx_output_env_t                        *oenv_;

};

RunnerModule::RunnerModule(IEnergyAnalysisPointer module) : module_(std::move(module)), timeUnit_(TimeUnit_Default)

{
    // Options for input files
    t0_       = -1;
    t1_       = -1;
    tDelta_   = 0;
    bVerbose_ = true;
    bView_    = false;

    output_env_init(&oenv_, gmx::getProgramContext(),
                    time_ps, // Make sure the defaults match.
                    bView_,
                    static_cast<xvg_format_t>(plotSettings_.plotFormat()),
                    bVerbose_ ? 1 : 0);
}

void RunnerModule::initOptions(IOptionsContainer                 *options,
                               ICommandLineOptionsModuleSettings *settings)
{
    options->addOption(FileNameOption("f")
                           .filetype(eftEnergy)
                           .inputFile()
                           .store(&fnEnergy_)
                           .defaultBasename("ener")
                           .description("Energy file")
                           .required());
    // Add options for energy file time control.
    options->addOption(DoubleOption("b").store(&t0_).timeValue()
                           .description("First frame (%t) to read from energy file"));
    options->addOption(DoubleOption("e").store(&t1_).timeValue()
                           .description("Last frame (%t) to read from energy file"));
    options->addOption(DoubleOption("dt").store(&tDelta_).timeValue()
                           .description("Only use frame if t MOD dt == first time (%t)"));
    options->addOption(BooleanOption("w").store(&bView_)
                           .description("View output [TT].xvg[tt], [TT].xpm[tt], "
                                        "[TT].eps[tt] and [TT].pdb[tt] files"));
    options->addOption(BooleanOption("v").store(&bVerbose_)
                           .description("Verbose output"));

    // Add time unit option.
    std::shared_ptr<TimeUnitBehavior>        timeUnitBehavior(
            new TimeUnitBehavior());
    timeUnitBehavior->setTimeUnitFromEnvironment();
    timeUnitBehavior->addTimeUnitOption(options, "tu");
    timeUnitBehavior->setTimeUnitStore(&timeUnit_);

    settings->addOptionsBehavior(timeUnitBehavior);
    plotSettings_.initOptions(options);

    // Call the module to do it's bit
    module_->initOptions(options, settings);
}

int RunnerModule::checkTime(double t)
{
    if ((t0_ >= 0) && (t < t0_))
    {
        return -1;
    }
    else if ((t1_ >= 0) && (t > t1_))
    {
        return 1;
    }
    return 0;
}

void RunnerModule::doInitModule(ener_file_t fp)
{
    try
    {
        std::vector<EnergyNameUnit>  eNU;
        {
            int          nre;
            gmx_enxnm_t *enm = nullptr;
            do_enxnms(fp, &nre, &enm);
            for (int i = 0; (i < nre); i++)
            {
                EnergyNameUnit enu;
                enu.energyName = enm[i].name;
                enu.energyUnit = enm[i].unit;
                eNU.push_back(enu);
            }
            free_enxnms(nre, enm);
        }
        module_->initAnalysis(eNU, oenv_);
    }
    GMX_CATCH_ALL_AND_EXIT_WITH_FATAL_ERROR;
}

//! \libinternal\brief Wrapper for deleting t_enxframe
void my_free_enxframe(t_enxframe *frame)
{
    free_enxframe(frame);
    delete frame;
}

int RunnerModule::run()
{
    if (t0_ >= 0)
    {
        printf("Will start reading at %g ps\n", t0_);
    }
    if (t1_ >= 0)
    {
        printf("Will end reading at %g ps\n", t1_);
    }

    // Set the energy terms
    gmx::unique_cptr<struct ener_file, done_ener_file> fp;
    fp.reset(open_enx(fnEnergy_.c_str(), "r"));
    doInitModule(fp.get());
    gmx::unique_cptr<t_enxframe, my_free_enxframe> enxframeGuard;
    enxframeGuard.reset(new t_enxframe);
    t_enxframe *frame = enxframeGuard.get();
    init_enxframe(frame);

    int64_t nframes = 0;
    while (do_enx(fp.get(), frame))
    {
        /* This loop searches for the first frame (when -b option is given),
         * or when this has been found it reads just one energy frame
         */
        if (0 == checkTime(frame->t))
        {
            module_->analyzeFrame(frame, oenv_);
            nframes++;
        }
    }

    /* Printing a new line, just because the gromacs library prints step info
     * while reading.
     */
    char buf[256];
    fprintf(stderr, "\nRead %s frames from %s\n",
            gmx_step_str(nframes, buf), fnEnergy_.c_str() );

    // Finally finish the analysis!
    module_->finalizeAnalysis(oenv_);

    // Let's see what we've got!
    module_->viewOutput(oenv_);

    output_env_done(oenv_);

    return 0;
}

}       // namespace

// static
void
EnergyAnalysisRunner::registerModule(
        CommandLineModuleManager *manager, const char *name,
        const char *description, const ModuleFactoryMethod &factory)
{
    auto runnerFactory = [factory]
    {
        return createModule(factory());
    };
    ICommandLineOptionsModule::registerModuleFactory(
            manager, name, description, runnerFactory);
}

// static
std::unique_ptr<ICommandLineOptionsModule>
EnergyAnalysisRunner::createModule(IEnergyAnalysisPointer module)
{
    return ICommandLineOptionsModulePointer(new RunnerModule(std::move(module)));
}

} // namespace gmx
