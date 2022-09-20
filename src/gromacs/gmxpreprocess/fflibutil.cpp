/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2010- The GROMACS Authors
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
#include "gmxpre.h"

#include "fflibutil.h"

#include <cstring>

#include <string>
#include <vector>

#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/datafilefinder.h"
#include "gromacs/utility/dir_separator.h"
#include "gromacs/utility/directoryenumerator.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/futil.h"
#include "gromacs/utility/path.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/utility/stringutil.h"

const char* fflib_forcefield_dir_ext()
{
    return ".ff";
}

const char* fflib_forcefield_itp()
{
    return "forcefield.itp";
}

const char* fflib_forcefield_doc()
{
    return "forcefield.doc";
}

void fflib_filename_base(const std::filesystem::path& filename, char* filebase, int maxlen)
{
    const char* cptr;
    char*       ptr;

    cptr = strrchr(filename.string().c_str(), DIR_SEPARATOR);
    if (cptr != nullptr)
    {
        /* Skip the separator */
        cptr += 1;
    }
    else
    {
        cptr = filename.string().c_str();
    }
    if (filename.string().size() >= static_cast<size_t>(maxlen))
    {
        gmx_fatal(FARGS, "filename is longer (%zu) than maxlen (%d)", filename.string().size(), maxlen);
    }
    strcpy(filebase, cptr);
    /* Remove the extension */
    ptr = strrchr(filebase, '.');
    if (ptr != nullptr)
    {
        ptr[0] = '\0';
    }
}

std::vector<std::filesystem::path> fflib_search_file_end(const std::filesystem::path& ffdir,
                                                         const char*                  file_end,
                                                         bool                         bFatalError)
{
    try
    {
        auto ffdirFull = (gmx::getLibraryFileFinder().findFile(ffdir));
        auto result = gmx::DirectoryEnumerator::enumerateFilesWithExtension(ffdirFull, file_end, true);
        if (result.empty() && bFatalError)
        {
            std::string message = gmx::formatString(
                    "Could not find any files ending on '%s' "
                    "in the force field directory '%s'",
                    file_end,
                    ffdir.c_str());
            GMX_THROW(gmx::InvalidInputError(message));
        }
        for (auto& filename : result)
        {
            filename = std::filesystem::path(ffdir).append(filename.string());
        }
        return result;
    }
    GMX_CATCH_ALL_AND_EXIT_WITH_FATAL_ERROR
}

std::vector<gmx::DataFileInfo> fflib_enumerate_forcefields()
{
    const char* const              dirend     = fflib_forcefield_dir_ext();
    const char* const              filename   = fflib_forcefield_itp();
    std::vector<gmx::DataFileInfo> candidates = gmx::getLibraryFileFinder().enumerateFiles(
            gmx::DataFileOptions(dirend).throwIfNotFound(false));

    std::vector<gmx::DataFileInfo> result;
    for (const auto& candidate : candidates)
    {
        auto testPath(std::filesystem::path(candidate.dir).append(candidate.name.string()).append(filename));
        // TODO: Consider also checking that the directory can be listed.
        if (gmx::File::exists(testPath, gmx::File::returnFalseOnError))
        {
            result.push_back(candidate);
        }
    }

    // TODO: Consider merging this into enumerateFiles(), such that the error
    // could also list the directories searched.
    if (result.empty())
    {
        std::string message = gmx::formatString(
                "No force fields found (files with name '%s' "
                "in subdirectories ending on '%s')",
                filename,
                dirend);
        GMX_THROW(gmx::InvalidInputError(message));
    }

    return result;
}

bool fflib_fexist(const std::string& file)
{
    return !gmx::findLibraryFile(file, true, false).empty();
}


FILE* fflib_open(const std::string& file)
{
    std::string fileFullPath = gmx::findLibraryFile(file);
    fprintf(stderr, "Opening force field file %s\n", fileFullPath.c_str());
    return gmx_ffopen(fileFullPath, "r");
}
