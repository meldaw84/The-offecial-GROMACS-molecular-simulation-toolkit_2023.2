/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2016- The GROMACS Authors
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
 * Declares gmx::IForceProvider and ForceProviders.
 *
 * See \ref page_mdmodules for an overview of this and associated interfaces.
 *
 * \author Teemu Murtola <teemu.murtola@gmail.com>
 * \author Carsten Kutzner <ckutzne@gwdg.de>
 * \inlibraryapi
 * \ingroup module_mdtypes
 */
#ifndef GMX_MDTYPES_IFORCEPROVIDER_H
#define GMX_MDTYPES_IFORCEPROVIDER_H

#include <memory>

#include "gromacs/math/vec.h"
#include "gromacs/utility/arrayref.h"
#include "gromacs/utility/gmxassert.h"

struct gmx_enerdata_t;
struct t_commrec;
struct t_forcerec;

namespace gmx
{

template<typename T>
class ArrayRef;
class ForceWithVirial;


/*! \libinternal \brief
 * Helper struct that bundles data for passing it over to the force providers
 *
 * This is a short-lived container that bundles up all necessary input data for the
 * force providers. Its only purpose is to allow calling forceProviders->calculateForces()
 * with just two arguments, one being the container for the input data,
 * the other the container for the output data.
 *
 * Both ForceProviderInput as well as ForceProviderOutput only package existing
 * data structs together for handing it over to calculateForces(). Apart from the
 * POD entries they own nothing.
 */
class ForceProviderInput
{
public:
    /*! \brief The constructor assembles all possible force provider input data
     *
     * This constructor should only be used in the main MD loop.
     * It collects all the data that can be used by the individual force providers.
     *
     * \param[in]  x        Atomic positions.
     * \param[in]  homenr   Number of atoms on the domain.
     * \param[in]  chargeA  Atomic charges for atoms on the domain.
     * \param[in]  massT    Atomic masses for atoms on the domain.
     * \param[in]  time     The current time in the simulation.
     * \param[in]  step     The current step in the simulation.
     * \param[in]  box      The simulation box.
     * \param[in]  cr       Communication structure for parallel runs.
     */
    ForceProviderInput(ArrayRef<const RVec> x,
                       int                  homenr,
                       ArrayRef<const real> chargeA,
                       ArrayRef<const real> massT,
                       double               time,
                       int64_t              step,
                       const matrix         box,
                       const t_commrec&     cr) :
        x_(x), homenr_(homenr), chargeA_(chargeA), massT_(massT), t_(time), step_(step), cr_(cr)
    {
        copy_mat(box, box_);
    }

    /*! \brief Basic constructor for individual force providers that do not need all parameters
     *
     * This constructor should be used for calls to an individual force provider,
     * as e.g. in the tests. Here typically only a part of the parameters that get
     * passed to the above full constructor is actually needed. After calling
     * the basic constructor ForceProviderInput fpi(cr),
     * additional required data can be passed by setting the public variables, e.g.
     * fpi.step_ = step.
     * If future force providers require additional data, only the full constructor call
     * in the main MD loop has to be expanded with the new parameter, but no changes will
     * have to be done in the tests of unrelated force providers.
     *
     * \param[in]  cr       Communication structure for parallel runs.
     */
    explicit ForceProviderInput(const t_commrec& cr) : cr_(cr) {}

    ArrayRef<const RVec> x_;            //!< The atomic positions
    int                  homenr_ = 0;   //!< Number of atoms on the domain.
    ArrayRef<const real> chargeA_{};    //!< Atomic charges for atoms on the domain.
    ArrayRef<const real> massT_{};      //!< Atomic masses for atoms on the domain.
    double               t_    = 0.0;   //!< The current time in the simulation
    int64_t              step_ = 0;     //!< The current step in the simulation
    matrix               box_{ { 0 } }; //!< The simulation box
    const t_commrec&     cr_;           //!< Communication structure for parallel runs.
};

/*! \brief Take pointer, check if valid, return reference
 */
template<class T>
T& makeRefFromPointer(T* ptr)
{
    GMX_ASSERT(ptr != nullptr, "got null pointer");
    return *ptr;
}

/*! \libinternal \brief
 * Helper struct bundling the output data of a force provider
 *
 * Same as for the ForceProviderInput class, but these variables can be written as well.
 */
class ForceProviderOutput
{
public:
    /*! \brief Constructor assembles all necessary force provider output data
     *
     * \param[in,out]  forceWithVirial  Container for force and virial
     * \param[in,out]  enerd            Structure containing energy data
     */
    ForceProviderOutput(ForceWithVirial* forceWithVirial, gmx_enerdata_t* enerd) :
        forceWithVirial_(makeRefFromPointer(forceWithVirial)), enerd_(makeRefFromPointer(enerd))
    {
    }

    ForceWithVirial& forceWithVirial_; //!< Container for force and virial
    gmx_enerdata_t&  enerd_;           //!< Structure containing energy data
};


/*! \libinternal \brief
 * Interface for a component that provides forces during MD.
 *
 * Modules implementing IMDModule generally implement this internally, and use
 * IMDModule::initForceProviders() to register their implementation in
 * ForceProviders.
 *
 * The interface most likely requires additional generalization for use in
 * other modules than the current electric field implementation.
 *
 * The forces that are produced by force providers are not taken into account
 * in the calculation of the virial. When applicable, the provider should
 * compute its own virial contribution.
 *
 * \inlibraryapi
 * \ingroup module_mdtypes
 */
class IForceProvider
{
public:
    /*! \brief
     * Computes forces.
     *
     * \param[in]    forceProviderInput    struct that collects input data for the force providers
     * \param[in,out] forceProviderOutput   struct that collects output data of the force providers
     */
    virtual void calculateForces(const ForceProviderInput& forceProviderInput,
                                 ForceProviderOutput*      forceProviderOutput) = 0;

protected:
    ~IForceProvider() {}
};

/*! \libinternal \brief
 * Evaluates forces from a collection of gmx::IForceProvider.
 *
 * \inlibraryapi
 * \ingroup module_mdtypes
 */
class ForceProviders
{
public:
    ForceProviders();
    ~ForceProviders();

    /*! \brief
     * Adds a provider.
     */
    void addForceProvider(gmx::IForceProvider* provider);

    //! Whether there are modules added.
    bool hasForceProvider() const;

    //! Computes forces.
    void calculateForces(const gmx::ForceProviderInput& forceProviderInput,
                         gmx::ForceProviderOutput*      forceProviderOutput) const;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace gmx

#endif
