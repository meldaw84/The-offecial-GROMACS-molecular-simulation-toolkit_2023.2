/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright 2012- The GROMACS Authors
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

/* Doxygen gets confused (buggy) about the block in this file in combination with
 * the  namespace prefix, and thinks store is documented here.
 * This will solve itself with the second-generation nbnxn kernels, so for now
 * we just tell Doxygen to stay out.
 */
#ifndef DOXYGEN

/* This is the innermost loop contents for the NBNxM SIMD kernels.
 */
{
    /* Inner loop specific constexpr variables */
    static_assert(nR % 2 == 0);
    constexpr int c_nRLJ = (c_iLJInteractions == ILJInteractions::None
                                    ? 0
                                    : nR / (c_iLJInteractions == ILJInteractions::Half ? 2 : 1));
    /* When calculating RF or Ewald interactions we calculate the electrostatic/LJ
     * forces on excluded atom pairs here in the non-bonded loops.
     * But when energies and/or virial is required we calculate them
     * separately to as then it is easier to separate the energy and virial
     * contributions.
     */
    constexpr bool c_haveExclusionForces =
            (c_calculateCoulombInteractions || haveLJEwaldGeometric) && c_needToCheckExclusions;

    /* The force times 1/r */
    std::array<SimdReal, nR> fScalarV;

    /* j-cluster index */
    const int cj = l_cj.cj(cjind);

    /* Indexing code in the rest of the file currently uses the following assumption */
    static_assert(UNROLLJ == STRIDE || (2 * UNROLLJ == STRIDE && UNROLLJ == 2));

    /* Atom indices (of the first atom in the cluster) */
    const int gmx_unused aj = cj * UNROLLJ;

    const int ajx = (UNROLLJ == STRIDE ? aj * DIM : (cj >> 1) * DIM * STRIDE + (cj & 1) * UNROLLJ);
    const int ajy = ajx + STRIDE;
    const int ajz = ajy + STRIDE;

    /* Interaction (non-exclusion) mask of all 1's or 0's */
    const auto interactV = loadSimdPairInteractionMasks<c_needToCheckExclusions, kernelLayout>(
            l_cj.excl(cjind), exclusionFilterV);

    /* load j atom coordinates */
    SimdReal jx_S = loadJAtomData<kernelLayout>(x, ajx);
    SimdReal jy_S = loadJAtomData<kernelLayout>(x, ajy);
    SimdReal jz_S = loadJAtomData<kernelLayout>(x, ajz);

    /* Calculate distance */
    const auto dxV = genArr<nR>([&](int i) { return ixV[i] - jx_S; });
    const auto dyV = genArr<nR>([&](int i) { return iyV[i] - jy_S; });
    const auto dzV = genArr<nR>([&](int i) { return izV[i] - jz_S; });

    /* rsq = dx*dx + dy*dy + dz*dz */
    auto rSquaredV = genArr<nR>([&](int i) { return norm2(dxV[i], dyV[i], dzV[i]); });

    /* Do the cut-off check */
    auto withinCutoffV = genBoolArr<nR>([&](int i) { return rSquaredV[i] < cutoffSquared; });

    if constexpr (c_needToCheckExclusions)
    {
        if constexpr (c_haveExclusionForces)
        {
            /* Only remove the (sub-)diagonal to avoid double counting exclusion forces */
            diagonalMasker.maskArray(ci_sh, cj, withinCutoffV);
        }
        else
        {
            /* No exclusion forces: remove all excluded atom pairs from the list */
            withinCutoffV = genBoolArr<nR>([&](int i) { return withinCutoffV[i] && interactV[i]; });
        }
    }

#    ifdef COUNT_PAIRS
    npair += pairCountWithinCutoff(rSquaredV, cutoffSquared);
#    endif

    // Ensure the distances do not fall below the limit where r^-12 overflows.
    // This should never happen for normal interactions.
    rSquaredV = genArr<nR>([&](int i) { return gmx::max(rSquaredV[i], minDistanceSquared); });

    /* Calculate 1/r */
    std::array<SimdReal, nR> rInvV;
#    if !GMX_DOUBLE
    rInvV = genArr<nR>([&](int i) { return invsqrt(rSquaredV[i]); });
#    else
    for (int i = 0; i < nR; i += 2)
    {
        invsqrtPair(rSquaredV[i], rSquaredV[i + 1], &rInvV[i], &rInvV[i + 1]);
    }
#    endif

    std::array<SimdReal, nR> qqV;
    if constexpr (c_calculateCoulombInteractions)
    {
        /* Load parameters for j atom */
        const SimdReal jq_S = loadJAtomData<kernelLayout>(q, aj);
        qqV                 = genArr<nR>([&](int i) { return chargeIV[i] * jq_S; });
    }

    /* Set rinv to zero for r beyond the cut-off */
    rInvV = genArr<nR>([&](int i) { return selectByMask(rInvV[i], withinCutoffV[i]); });

    const auto rInvSquaredV = genArr<nR>([&](int i) { return rInvV[i] * rInvV[i]; });

    /* frcoul = qi*qj*(1/r - fsub)*r */
    std::array<SimdReal, nR> frCoulombV;
    std::array<SimdReal, nR> vCoulombV;

    if constexpr (c_calculateCoulombInteractions)
    {
        /* Note that here we calculate force*r, not the usual force/r.
         * This allows avoiding masking the reaction-field contribution,
         * as frcoul is later multiplied by rinvsq which has been
         * masked with the cut-off check.
         */

        /* Only add 1/r for non-excluded atom pairs */
        std::array<SimdReal, nR> rInvExclV;
        if constexpr (c_haveExclusionForces)
        {
            rInvExclV = genArr<nR>([&](int i) { return selectByMask(rInvV[i], interactV[i]); });
        }
        else
        {
            /* We hope that the compiler optimizes rInvExclV away */
            rInvExclV = rInvV;
        }

        /* Electrostatic interactions, frcoul =  qi*qj*(1/r - fsub)*r */
        if constexpr (!calculateEnergies)
        {
            frCoulombV = coulombCalculator.template force<nR>(rSquaredV, rInvV, rInvExclV, withinCutoffV);

            frCoulombV = genArr<nR>([&](int i) { return qqV[i] * frCoulombV[i]; });
        }
        else
        {
            // The potential (RF or Ewald reciprocal) we need to subtract from 1/r
            std::array<SimdReal, nR> vCoulombCorrectionV;

            coulombCalculator.template forceAndCorrectionEnergy<nR>(
                    rSquaredV, rInvV, rInvExclV, withinCutoffV, frCoulombV, vCoulombCorrectionV);

            frCoulombV = genArr<nR>([&](int i) { return qqV[i] * frCoulombV[i]; });

            if constexpr (coulombType != KernelCoulombType::RF)
            {
                if constexpr (c_needToCheckExclusions)
                {
                    vCoulombCorrectionV = genArr<nR>([&](int i) {
                        return vCoulombCorrectionV[i] + selectByMask(ewaldShift, interactV[i]);
                    });
                }
                else
                {
                    vCoulombCorrectionV =
                            genArr<nR>([&](int i) { return vCoulombCorrectionV[i] + ewaldShift; });
                }
            }

            /* Combine Coulomb and correction terms */
            vCoulombV = genArr<nR>(
                    [&](int i) { return qqV[i] * (rInvExclV[i] - vCoulombCorrectionV[i]); });

            /* Mask energy for cut-off and diagonal */
            vCoulombV =
                    genArr<nR>([&](int i) { return selectByMask(vCoulombV[i], withinCutoffV[i]); });
        }
    }

    /* Lennard-Jones interaction */
    constexpr bool calculateLJInteractions = (c_iLJInteractions != ILJInteractions::None);
    std::array<SimdReal, calculateLJInteractions ? c_nRLJ : 0> gmx_unused frLJV;
    std::array<SimdReal, (calculateLJInteractions && calculateEnergies) ? c_nRLJ : 0> gmx_unused vLJV;

    if constexpr (calculateLJInteractions)
    {
        std::array<SimdBool, c_nRLJ> withinVdwCutoffV;
        if constexpr (haveVdwCutoffCheck)
        {
            withinVdwCutoffV =
                    genBoolArr<c_nRLJ>([&](int i) { return rSquaredV[i] < vdwCutoffSquared; });
        }

        /* Index for loading LJ parameters, complicated when interleaving */
        int aj2;
        if constexpr (ljCombinationRule != LJCombinationRule::None || haveLJEwaldGeometric)
        {
            if constexpr (GMX_SIMD_REAL_WIDTH == GMX_SIMD_J_UNROLL_SIZE * STRIDE)
            {
                aj2 = aj * 2;
            }
            else
            {
                aj2 = (cj >> 1) * 2 * STRIDE + (cj & 1) * UNROLLJ;
            }
        }

        if constexpr (ljCombinationRule != LJCombinationRule::LorentzBerthelot)
        {
            /* We use C6 and C12 */
            std::array<SimdReal, c_nRLJ> c6V;
            std::array<SimdReal, c_nRLJ> c12V;

            if constexpr (ljCombinationRule == LJCombinationRule::None)
            {
                // Load 6*C6 and 6*C12 for all pairs
                for (int i = 0; i < c_nRLJ; i++)
                {
                    if constexpr (kernelLayout == KernelLayout::r2xMM)
                    {
                        gatherLoadTransposeHsimd<c_simdBestPairAlignment>(
                                nbfpI[i * 2], nbfpI[i * 2 + 1], type + aj, &c6V[i], &c12V[i]);
                    }
                    else
                    {
                        gatherLoadTranspose<c_simdBestPairAlignment>(
                                nbfpI[i], type + aj, &c6V[i], &c12V[i]);
                    }
                }
            }

            if constexpr (ljCombinationRule == LJCombinationRule::Geometric)
            {
                // Load j-atom sqrt(6*C6) and sqrt(12*C12)
                SimdReal c6J  = loadJAtomData<kernelLayout>(ljc, aj2 + 0);
                SimdReal c12J = loadJAtomData<kernelLayout>(ljc, aj2 + STRIDE);
                // Compute the combined 6*C6 and 12*C12
                c6V  = genArr<c_nRLJ>([&](int i) { return c6GeomV[i] * c6J; });
                c12V = genArr<c_nRLJ>([&](int i) { return c12GeomV[i] * c12J; });
            }

            // Compute the Lennard Jones force and optionally the energy
            ljCalculator.template forceC6C12<c_nRLJ, c_haveExclusionForces>(
                    rSquaredV, rInvV, rInvSquaredV, interactV, c6V, c12V, sixth_S, twelveth_S, frLJV, vLJV);
        }

        if constexpr (ljCombinationRule == LJCombinationRule::LorentzBerthelot)
        {
            const SimdReal halfSigmaJ   = loadJAtomData<kernelLayout>(ljc, aj2 + 0);
            const SimdReal sqrtEpsilonJ = loadJAtomData<kernelLayout>(ljc, aj2 + STRIDE);

            const auto sigmaV = genArr<c_nRLJ>([&](int i) { return halfSigmaIV[i] + halfSigmaJ; });
            const auto epsilonV =
                    genArr<c_nRLJ>([&](int i) { return sqrtEpsilonIV[i] * sqrtEpsilonJ; });

            ljCalculator.template forceSigmaEpsilon<c_nRLJ, c_haveExclusionForces, haveVdwCutoffCheck>(
                    rInvV, interactV, withinVdwCutoffV.data(), sigmaV, epsilonV, sixth_S, twelveth_S, frLJV, vLJV);
        }

        if constexpr (calculateEnergies && c_needToCheckExclusions)
        {
            /* The potential shift should be removed for excluded pairs */
            for (int i = 0; i < c_nRLJ; i++)
            {
                vLJV[i] = selectByMask(vLJV[i], interactV[i]);
            }
        }

        if constexpr (haveLJEwaldGeometric)
        {
            /* Determine C6 for the grid using the geometric combination rule */
            const SimdReal c6J     = loadJAtomData<kernelLayout>(ljc, aj2 + 0);
            const auto     c6GridV = genArr<c_nRLJ>([&](int i) { return c6GeomV[i] * c6J; });

            addLennardJonesEwaldCorrections<c_nRLJ, c_needToCheckExclusions, calculateEnergies>(
                    rSquaredV, rInvSquaredV, interactV, withinCutoffV.data(), c6GridV, ljEwaldParams, sixth_S, frLJV, vLJV);
        }

        if constexpr (haveVdwCutoffCheck)
        {
            /* frLJ is multiplied later by rinvsq, which is masked for the Coulomb
             * cut-off, but if the VdW cut-off is shorter, we need to mask with that.
             */
            for (int i = 0; i < c_nRLJ; i++)
            {
                frLJV[i] = selectByMask(frLJV[i], withinVdwCutoffV[i]);
            }
        }

        if constexpr (calculateEnergies)
        {
            /* The potential shift should be removed for pairs beyond cut-off */
            for (int i = 0; i < c_nRLJ; i++)
            {
                vLJV[i] = selectByMask(vLJV[i],
                                       haveVdwCutoffCheck ? withinVdwCutoffV[i] : withinCutoffV[i]);
            }
        }

    } // calculateLJInteractions

    if constexpr (calculateEnergies)
    {
        /* Energy group indices for two atoms packed into one int */
        std::array<int, useEnergyGroups ? UNROLLJ / 2 : 0> gmx_unused egp_jj;

        if constexpr (useEnergyGroups)
        {
            /* Extract the group pair index per j pair.
             * Energy groups are stored per i-cluster, so things get
             * complicated when the i- and j-cluster size don't match.
             */
            // static_assert(UNROLLJ == 2 || UNROLLI <= UNROLLJ);

            if constexpr (UNROLLJ == 2)
            {
                const int egps_j = nbatParams.energrp[cj >> 1];
                egp_jj[0] = ((egps_j >> ((cj & 1) * egps_jshift)) & egps_jmask) * egps_jstride;
            }
            else
            {
                for (int jdi = 0; jdi < UNROLLJ / UNROLLI; jdi++)
                {
                    const int egps_j = nbatParams.energrp[cj * (UNROLLJ / UNROLLI) + jdi];
                    for (int jj = 0; jj < (UNROLLI / 2); jj++)
                    {
                        egp_jj[jdi * (UNROLLI / 2) + jj] =
                                ((egps_j >> (jj * egps_jshift)) & egps_jmask) * egps_jstride;
                    }
                }
            }
        }

        if constexpr (c_calculateCoulombInteractions)
        {
            // Accumulate the Coulomb energies
            if constexpr (!useEnergyGroups)
            {
                for (int i = 0; i < nR; i++)
                {
                    vctot_S = vctot_S + vCoulombV[i];
                }
            }
            else
            {
                for (int i = 0; i < nR; i++)
                {
                    if constexpr (kernelLayout == KernelLayout::r2xMM)
                    {
                        accumulateGroupPairEnergies2xMM<kernelLayout>(
                                vCoulombV[i], vctp[i * 2], vctp[i * 2 + 1], egp_jj);
                    }
                    else
                    {
                        accumulateGroupPairEnergies4xM<kernelLayout>(vCoulombV[i], vctp[i], egp_jj);
                    }
                }
            }
        }

        if constexpr (c_iLJInteractions != ILJInteractions::None)
        {
            // Accumulate the Lennard-Jones energies
            if constexpr (!useEnergyGroups)
            {
                for (int i = 0; i < c_nRLJ; i++)
                {
                    Vvdwtot_S = Vvdwtot_S + vLJV[i];
                }
            }
            else
            {
                for (int i = 0; i < c_nRLJ; i++)
                {
                    if constexpr (kernelLayout == KernelLayout::r2xMM)
                    {
                        accumulateGroupPairEnergies2xMM<kernelLayout>(
                                vLJV[i], vvdwtp[i * 2], vvdwtp[i * 2 + 1], egp_jj);
                    }
                    else
                    {
                        accumulateGroupPairEnergies4xM<kernelLayout>(vLJV[i], vvdwtp[i], egp_jj);
                    }
                }
            }
        }

    } // calculateEnergies

    if constexpr (c_iLJInteractions != ILJInteractions::None)
    {
        if constexpr (c_calculateCoulombInteractions)
        {
            for (int i = 0; i < c_nRLJ; i++)
            {
                fScalarV[i] = rInvSquaredV[i] * (frCoulombV[i] + frLJV[i]);
            }
            for (int i = c_nRLJ; i < nR; i++)
            {
                fScalarV[i] = rInvSquaredV[i] * frCoulombV[i];
            }
        }
        else
        {
            for (int i = 0; i < c_nRLJ; i++)
            {
                fScalarV[i] = rInvSquaredV[i] * frLJV[i];
            }
        }
    }
    else
    {
        for (int i = c_nRLJ; i < nR; i++)
        {
            fScalarV[i] = rInvSquaredV[i] * frCoulombV[i];
        }
    }

    /* Calculate temporary vectorial force */
    const auto txV = genArr<nR>([&](int i) { return fScalarV[i] * dxV[i]; });
    const auto tyV = genArr<nR>([&](int i) { return fScalarV[i] * dyV[i]; });
    const auto tzV = genArr<nR>([&](int i) { return fScalarV[i] * dzV[i]; });

    /* Increment i atom force */
    forceIXV = genArr<nR>([&](int i) { return forceIXV[i] + txV[i]; });
    forceIYV = genArr<nR>([&](int i) { return forceIYV[i] + tyV[i]; });
    forceIZV = genArr<nR>([&](int i) { return forceIZV[i] + tzV[i]; });

    /* Decrement j atom force */
    if constexpr (kernelLayout == KernelLayout::r2xMM)
    {
        decr3Hsimd(f + aj * DIM, sum(txV), sum(tyV), sum(tzV));
    }
    else
    {
        store(f + ajx, load<SimdReal>(f + ajx) - sum(txV));
        store(f + ajy, load<SimdReal>(f + ajy) - sum(tyV));
        store(f + ajz, load<SimdReal>(f + ajz) - sum(tzV));
    }
}

#endif // !DOXYGEN
