/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * GROningen MAchine for Chemical Simulation
 */

#ifndef _config_h
#define _config_h

static char *SRCID_config_h = "$Id$";

#ifdef HAVE_IDENT
#ident	"@(#) config.h 1.6 11/23/92"
#endif /* HAVE_IDENT */

/*
 * #ifdef _sun_
 * #define DOUBLE
 * #endif
 * 
 * #ifdef _860_
 * #define VECLIB
 * #endif
 */

#ifdef _386_
#define HAVE_ULONG
#endif

#define HAVE_SEMA

#endif	/* _config_h */
