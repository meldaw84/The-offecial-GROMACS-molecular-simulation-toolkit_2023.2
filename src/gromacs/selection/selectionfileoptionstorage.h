/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014,2016,2018 by the GROMACS development team.
 * Copyright (c) 2019,2020, by the GROMACS development team, led by
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
/*! \internal \file
 * \brief
 * Declares gmx::SelectionFileOptionStorage.
 *
 * \author Teemu Murtola <teemu.murtola@gmail.com>
 * \ingroup module_selection
 */
#ifndef GMX_SELECTION_SELECTIONFILEOPTIONSTORAGE_H
#define GMX_SELECTION_SELECTIONFILEOPTIONSTORAGE_H

#include "gromacs/options/abstractoptionstorage.h"
#include "selectionfileoption.h"

namespace gmx
{

class SelectionFileOption;
class SelectionOptionManager;

/*! \internal \brief
 * Implementation for a special option for reading selections from files.
 *
 * \ingroup module_selection
 */
class SelectionFileOptionStorage : public AbstractOptionStorage
{
public:
    /*! \brief
     * Initializes the storage from option settings.
     *
     * \param[in] settings   Storage settings.
     * \param     manager    Manager for this object.
     */
    SelectionFileOptionStorage(const SelectionFileOption& settings, SelectionOptionManager* manager);

    OptionInfo&              optionInfo() override { return info_; }
    std::string              typeString() const override { return "file"; }
    int                      valueCount() const override { return 0; }
    std::vector<Any>         defaultValues() const override { return {}; }
    std::vector<std::string> defaultValuesAsStrings() const override { return {}; }
    std::vector<Any>         normalizeValues(const std::vector<Any>& values) const override
    {
        return values;
    }

private:
    void clearSet() override;
    void convertValue(const Any& value) override;
    void processSet() override;
    void processAll() override {}

    SelectionFileOptionInfo info_;
    SelectionOptionManager& manager_;
    bool                    bValueParsed_;
};

} // namespace gmx

#endif
