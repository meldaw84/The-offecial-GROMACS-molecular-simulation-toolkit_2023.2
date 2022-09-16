/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2018- The GROMACS Authors
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
/*! \file
 * \internal \brief
 * Implements classes in LocalAtomSet.h
 *
 * \author Christian Blau <cblau@gwdg.de>
 * \ingroup module_domdec
 */
#include "gmxpre.h"

#include "gromacs/domdec/localatomset.h"

#include "localatomsetdata.h"

namespace gmx
{

LocalAtomSet::LocalAtomSet(const internal::LocalAtomSetData& data) : data_(&data) {}

ArrayRef<const int> LocalAtomSet::globalIndex() const
{
    return data_->globalIndex_;
}

ArrayRef<const int> LocalAtomSet::localIndex() const
{
    return data_->localIndex_;
}

ArrayRef<const int> LocalAtomSet::collectiveIndex() const
{
    return data_->collectiveIndex_;
}

std::size_t LocalAtomSet::numAtomsGlobal() const
{
    return data_->globalIndex_.size();
}

std::size_t LocalAtomSet::numAtomsLocal() const
{
    return data_->localIndex_.size();
}

} // namespace gmx
