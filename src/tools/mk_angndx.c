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
 * Great Red Oystrich Makes All Chemists Sane
 */
static char *SRCID_mk_angndx_c = "$Id$";

#include "typedefs.h"
#include "smalloc.h"
#include "copyrite.h"
#include "statutil.h"
#include "statusio.h"
#include "macros.h"
#include "string2.h"
#include "futil.h"
#include "assert.h"
#include "fatal.h"

static int calc_nftype(int FTYPE,t_idef *idef)
{
  int i,nf=0;

  for(i=0; (i<idef->ntypes); i++)
    if (idef->functype[i] == FTYPE) 
      nf++;

  return nf;
}

static void fill_ft_ind(int FTYPE,int ft_ind[],t_idef *idef,char *grpnames[])
{
  char buf[125];
  int  i,ft,ind=0;

  for(i=0; (i<idef->ntypes); i++) {
    ft=idef->functype[i];
    if (ft == FTYPE) {
      ft_ind[i]=ind;
      switch (FTYPE) {
      case F_ANGLES:
	sprintf(buf,"Theta=%.1f",idef->iparams[i].harmonic.krA);
	break;
      case F_PDIHS:
	sprintf(buf,"Phi=%.1f",idef->iparams[i].pdihs.phiA);
	break;
      case F_IDIHS:
	sprintf(buf,"Xi=%.1f",idef->iparams[i].harmonic.krA);
	break;
      case F_RBDIHS:
	sprintf(buf,"RB-Dihs");
	break;
      default:
	fatal_error(0,"kjdshfgkajhgajytgajtgasuyf");
      }
      grpnames[ind]=strdup(buf);
      ind++;
    }
    else
      ft_ind[i]=-1;
  }
}

static void fill_ang(int FTYPE,int fac,
		     int nr[],int *index[],int ft_ind[],t_idef *idef)
{
  int     i,j,ft,fft,nr_fac;
  t_iatom *ia;

  ia=idef->il[FTYPE].iatoms;
  for(i=0; (i<idef->il[FTYPE].nr); ) {
    ft=idef->functype[ia[0]];
    fft=ft_ind[ia[0]];
    assert(fft != -1);
    nr_fac=fac*nr[fft];
    for(j=0; (j<fac); j++)
      index[fft][nr_fac+j]=ia[j+1];
    nr[fft]++;
    ia+=interaction_function[ft].nratoms+1;
    i+=interaction_function[ft].nratoms+1;
  }
}

int main(int argc,char *argv[])
{
  static char *desc[] = {
    "mk_angndx makes an index file for calculation of",
    "angle distributions etc. It uses a binary topology for the",
    "definitions of the angles, dihedrals etc."
  };
  static char *opt=NULL;
  t_pargs pa[] = {
    { "-type", FALSE, etSTR, &opt,
      "Select either A (angles), D (dihedrals), I (impropers), R (Ryckaert-Bellemans) or P (phi/psi)" }
  };
      
  FILE       *out;
  t_topology *top;
  int        i,j,nftype,nang;
  int        mult=0,FTYPE=0;
  bool       bPP;
  int        **index;
  int        *ft_ind;
  int        *nr;
  char       **grpnames;
  t_filenm fnm[] = {
    { efTPB, NULL, NULL, ffREAD  },
    { efNDX, NULL, NULL, ffWRITE }
  };
  
  CopyRight(stderr,argv[0]);
  parse_common_args(&argc,argv,0,FALSE,asize(fnm),fnm,asize(pa),pa,
		    asize(desc),desc,0,NULL);

  if (!opt)
    usage(argv[0],opt);
    
  bPP  = FALSE;
  mult = 4;
  switch(opt[0]) {
  case 'A':
    mult=3;
    FTYPE=F_ANGLES;
    break;
  case 'D':
    FTYPE=F_PDIHS;
    break;
  case 'I':
    FTYPE=F_IDIHS;
    break;
  case 'R':
    FTYPE=F_RBDIHS;
    break;
  case 'P':
    bPP=TRUE;
    break;
  default:
    usage(argv[0],opt);
  }

  top=read_top(fnm[0].fn);

  if (!bPP) {
    nftype=calc_nftype(FTYPE,&(top->idef));
    snew(grpnames,nftype);
    snew(ft_ind,top->idef.ntypes);
    fill_ft_ind(FTYPE,ft_ind,&top->idef,grpnames);
    nang=top->idef.il[FTYPE].nr;
    
    snew(nr,nftype);
    snew(index,nftype);
    for(i=0; (i<nftype); i++)
      snew(index[i],nang*mult);
    
    fill_ang(FTYPE,mult,nr,index,ft_ind,&(top->idef));

    out=ffopen(fnm[1].fn,"w");
    fprintf(out,"%10d%10d\n",nftype,nang*mult);
    for(i=0; (i<nftype); i++) {
      fprintf(out,"%12s  %d  ",grpnames[i],mult*nr[i]);
      for(j=0; (j<nr[i]*mult); j++) {
	fprintf(out,"%d  ",index[i][j]);
      }
      fprintf(out,"\n");
    }
    fclose(out);
  }
  else {
    fprintf(stderr,"Sorry, maybe later...\n");
  }
  
  thanx(stdout);
  
  return 0;
}
