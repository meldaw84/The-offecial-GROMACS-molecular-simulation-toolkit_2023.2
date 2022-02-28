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
/*! \internal \file
 * \brief
 * Implements classes in energytermcontainer.h.
 *
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 * \ingroup module_energyanalysis
 */
#include "gmxpre.h"

#include "energytermcontainer.h"

#include <cmath>
#include <cstring>

#include <algorithm>
#include <vector>

#include "gromacs/commandline/viewit.h"
#include "gromacs/fileio/enxio.h"
#include "gromacs/fileio/oenv.h"
#include "gromacs/fileio/trxio.h"
#include "gromacs/fileio/xvgr.h"
#include "gromacs/math/vec.h"
#include "gromacs/options/basicoptions.h"
#include "gromacs/options/filenameoption.h"
#include "gromacs/options/options.h"
#include "gromacs/statistics/statistics.h"
#include "gromacs/topology/idef.h"
#include "gromacs/topology/ifunc.h"
#include "gromacs/trajectory/energyframe.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/utility/snprintf.h"
#include "gromacs/utility/unique_cptr.h"

namespace gmx
{

void EnergyTermContainer::initOptions(IOptionsContainer* options)
{
    options->addOption(
            IntegerOption("nmol").store(&nMol_).description("Number of molecules in the system"));
    options->addOption(IntegerOption("nblocks").store(&nBlocks_).description(
            "Number of blocks for error analysis"));
}

void EnergyTermContainer::setStoreData(bool storeData)
{
    storeData_ = storeData;
    for (auto& eti : et_)
    {
        eti.setStoreData(storeData);
    }
}

void EnergyTermContainer::addFrame(t_enxframe* fr)
{
    for (auto& eti : et_)
    {
        unsigned int findex = eti.fileIndex();
        if (findex < static_cast<unsigned int>(fr->nre))
        {
            eti.addFrame(fr->t,
                         fr->step,
                         fr->nsum,
                         fr->ener[findex].esum,
                         fr->ener[findex].eav,
                         fr->ener[findex].e);
        }
    }
}

bool EnergyTermContainer::energyTerm(const std::string& term, double* e, double* stddev)
{
    auto eti = etSearch(term);
    if (end() != eti)
    {
        *e      = eti->average();
        *stddev = eti->standardDeviation();
        return true;
    }
    return false;
}

EnergyTermIterator EnergyTermContainer::etSearch(unsigned int findex)
{
    for (EnergyTermIterator eti = begin(); eti < end(); ++eti)
    {
        if (eti->fileIndex() == findex)
        {
            return eti;
        }
    }
    return end();
}

EnergyTermIterator EnergyTermContainer::etSearch(const std::string& eTerm)
{
    EnergyTermIterator eti;
    for (eti = begin(); (eti < end()); ++eti)
    {
        if (eti->name() == eTerm)
        {
            break;
        }
    }
    return eti;
}

bool EnergyTermContainer::energyTerm(unsigned int ftype, double* e, double* stddev)
{
    return energyTerm(interaction_function[ftype].longname, e, stddev);
}

void printStatistics(FILE* fp, ConstEnergyTermIterator eBegin, ConstEnergyTermIterator eEnd, unsigned int nBlocks)
{
    if (eEnd > eBegin)
    {
        char buf[256];

        fprintf(fp,
                "\nStatistics over %s steps [ %.4f through %.4f ps ], %u data sets\n",
                gmx_step_str(eBegin->numSteps(), buf),
                eBegin->timeBegin(),
                eBegin->timeEnd(),
                static_cast<unsigned int>(eEnd - eBegin));
        if (nBlocks > 1)
        {
            fprintf(fp,
                    "Error estimate based on averaging over %u blocks of %g ps.\n",
                    nBlocks,
                    eBegin->timeSpan() / nBlocks);
        }
        else
        {
            fprintf(fp, "Specify number of blocks in order to provide an error estimate.\n");
        }
        fprintf(fp,
                "%-24s %10s %10s %10s %10s\n",
                "Energy",
                "Average",
                "Err.Est.",
                "RMSD",
                "Tot-Drift");
        fprintf(fp, "--------------------------------------------------------------------\n");
        for (auto eti = eBegin; eti < eEnd; ++eti)
        {
            char drift[32];
            char errorEstimate[32];
            real a;
            if (eti->drift(&a))
            {
                snprintf(drift, sizeof(drift), "%10g", a * eti->timeSpan());
            }
            else
            {
                snprintf(drift, sizeof(drift), "N/A");
            }
            real ee;
            if (eti->errorEstimate(nBlocks, &ee))
            {
                snprintf(errorEstimate, sizeof(errorEstimate), "%10g", ee);
            }
            else
            {
                snprintf(errorEstimate, sizeof(errorEstimate), "N/A");
            }

            fprintf(fp,
                    "%-24s %10g %10s %10g %10s (%s)\n",
                    eti->name().c_str(),
                    eti->average(),
                    errorEstimate,
                    eti->standardDeviation(),
                    drift,
                    eti->unit().c_str());
        }
    }
    else
    {
        fprintf(fp, "There are no energy terms to be printed.\n");
    }
}

void printXvgLegend(FILE*                   fp,
                    ConstEnergyTermIterator eBegin,
                    ConstEnergyTermIterator eEnd,
                    const gmx_output_env_t* oenv)
{
    std::vector<std::string> legtmp;
    std::vector<const char*> leg;

    legtmp.reserve(static_cast<size_t>(eEnd - eBegin));
    leg.reserve(static_cast<size_t>(eEnd - eBegin));
    for (auto eti = eBegin; eti < eEnd; ++eti)
    {
        legtmp.push_back(eti->name());
        leg.push_back(legtmp.back().c_str());
    }
    xvgr_legend(fp, leg.size(), leg.data(), oenv);
}

void printEnergies(const std::string&      outputFile,
                   ConstEnergyTermIterator eBegin,
                   ConstEnergyTermIterator eEnd,
                   bool                    bDouble,
                   const gmx_output_env_t* oenv)
{
    if (eEnd > eBegin && eBegin->storeData())
    {
        gmx::unique_cptr<FILE, xvgrclose> fp(
                xvgropen(outputFile.c_str(), "Energy", "Time (ps)", "Unit", oenv));
        printXvgLegend(fp.get(), eBegin, eEnd, oenv);
        for (auto eti = eBegin; eti < eEnd; ++eti)
        {
            fprintf(fp.get(), "@type xy\n");
            for (int64_t i = 0; i < eti->numFrames(); i++)
            {
                if (bDouble)
                {
                    fprintf(fp.get(), "%15e  %15e\n", eti->findFrame(i)->time(), eti->findFrame(i)->energy());
                }
                else
                {
                    fprintf(fp.get(), "%10g  %10g\n", eti->findFrame(i)->time(), eti->findFrame(i)->energy());
                }
            }
            fprintf(fp.get(), "&\n");
        }
    }
    else
    {
        fprintf(stderr, "WARNING: Energies not stored, so I can not print them\n");
    }
}

void yAxis(ConstEnergyTermIterator eBegin, ConstEnergyTermIterator eEnd, std::string* yaxis)
{
    std::vector<std::string> units;
    for (auto eti = eBegin; eti < eEnd; ++eti)
    {
        if (std::find_if(units.begin(),
                         units.end(),
                         [eti](const std::string& str) { return str == eti->unit(); })
            == units.end())
        {
            units.push_back(eti->unit());
        }
    }
    yaxis->clear();
    for (auto& u : units)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "(%s)", u.c_str());
        if (!yaxis->empty())
        {
            yaxis->append(", ");
        }
        yaxis->append(buf);
    }
}

} // namespace gmx
