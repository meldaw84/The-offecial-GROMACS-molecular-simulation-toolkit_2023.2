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
 * Great Red Owns Many ACres of Sand 
 */
static char *SRCID_maths_c = "$Id$";

#include <math.h>
#include "maths.h"

int gmx_nint(real a)
{   
  const real half = .5;
  int   result;
  
  result = (a < 0.) ? ((int)(a - half)) : ((int)(a + half));
  return result;
}

real sqr(real x)
{
  return (x*x);
}

real sign(real x,real y)
{
  if (y < 0)
    return -fabs(x);
  else
    return +fabs(x);
}
