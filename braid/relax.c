/*BHEADER**********************************************************************
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC. 
 * Produced at the Lawrence Livermore National Laboratory.
 * 
 * This file is part of XBraid. For support, post issues to the XBraid Github page.
 * 
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License (as published by the Free Software
 * Foundation) version 2.1 dated February 1999.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the terms and conditions of the GNU General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 ***********************************************************************EHEADER*/

#include "_braid.h"
#include "util.h"

/*----------------------------------------------------------------------------
 * Do nu sweeps of F-then-C relaxation
 *----------------------------------------------------------------------------*/

braid_Int
_braid_FCRelax(braid_Core  core,
               braid_Int   level)
{
   braid_App       app      = _braid_CoreElt(core, app);
   braid_Int       nlevels  = _braid_CoreElt(core, nlevels);
   braid_Int      *nrels    = _braid_CoreElt(core, nrels);
   braid_Real     *CWts     = _braid_CoreElt(core, CWts);
   _braid_Grid   **grids    = _braid_CoreElt(core, grids);
   braid_Int       ncpoints = _braid_GridElt(grids[level], ncpoints);

   braid_BaseVector  u, u_old;
   braid_Real        CWt;
   braid_Int         flo, fhi, fi, ci;
   braid_Int         nu, nrelax, interval;

   nrelax  = nrels[level];
   CWt     = CWts[level];

   for (nu = 0; nu < nrelax; nu++)
   {
      _braid_UCommInit(core, level);

      /* Start from the right-most interval */
      for (interval = ncpoints; interval > -1; interval--)
      {
         _braid_GetInterval(core, level, interval, &flo, &fhi, &ci);

         if (flo <= fhi)
         {
            _braid_UGetVector(core, level, flo-1, &u);
         }
         else if (ci > _braid_CoreElt(core, initiali))
         {
            _braid_UGetVector(core, level, ci-1, &u);
         }

         /* F-relaxation */
         for (fi = flo; fi <= fhi; fi++)
         {
            _braid_Step(core, level, fi, NULL, u);
            _braid_USetVector(core, level, fi, u, 0);
         }

         /* C-relaxation */
         if (ci > _braid_CoreElt(core, initiali))
         {
            /* If weighted Jacobi, store the previous u-value,
             *   Note, do no weighting if coarsest level*/
            if( (CWt != 1.0) && (level != (nlevels-1)) )
            {
               _braid_UGetVector(core, level, ci, &u_old);
            }

            _braid_Step(core, level, ci, NULL, u);

            if( (CWt != 1.0) && (level != (nlevels-1)) )
            {
               /* Apply weighted combination for w-Jacobi
                * u <--  omega*u_new + (1-omega)*u_old */
               _braid_BaseSum(core, app, (1.0 - CWt), u_old, CWt, u);
               _braid_BaseFree(core, app, u_old);
            }

            _braid_USetVector(core, level, ci, u, 1);
         }

         /* if ((flo <= fhi) && (interval == ncpoints)) */
         if ((flo <= fhi) && !(ci > _braid_CoreElt(core, initiali)))
         {
            _braid_BaseFree(core, app,  u);
         }
      }
      _braid_UCommWait(core, level);
   }

   return _braid_error_flag;
}

/*----------------------------------------------------------------------------
 * Do nu sweeps of F-then-C relaxation followed by one final F-relaxation
 *----------------------------------------------------------------------------*/

braid_Int
_braid_TriFCFRelax(braid_Core  core,
                   braid_Int   level,
                   braid_Int   nrelax)
{
   braid_Int     *nrels   = _braid_CoreElt(core, nrels);
   _braid_Grid  **grids   = _braid_CoreElt(core, grids);
   braid_Int      ilower  = _braid_GridElt(grids[level], ilower);
   braid_Int      iupper  = _braid_GridElt(grids[level], iupper);
   braid_Int      cfactor = _braid_GridElt(grids[level], cfactor);

   braid_Int      nrequests;
   MPI_Request   *requests;
   MPI_Status    *statuses;
   void         **buffers;

   braid_Int      nu, i;

   if (ilower > iupper)
   {
      /* No data for this process on this level */
      return _braid_error_flag;
   }

   if (nrelax < 0)
   {
      /* use preset nrelax values */
      nrelax  = nrels[level];
   }

   for (nu = 0; nu <= nrelax; nu++)
   {
      /* F-points */

      /* Initiate communication (ignore F/C for now) and loop over center points */
      _braid_TriCommInit(core, level, &nrequests, &requests, &statuses, &buffers);
      for (i = (ilower+1); i <= (iupper-1); i++)
      {
         if ( _braid_IsFPoint(i, cfactor) )
         {
            _braid_TriSolve(core, level, i);
         }
      }
      /* Finalize communication and loop over end points */
      _braid_TriCommWait(core, level, nrequests, &requests, &statuses, &buffers);
      if ( _braid_IsFPoint(ilower, cfactor) )
      {
         _braid_TriSolve(core, level, ilower);
      }
      if ( (iupper > ilower) && _braid_IsFPoint(iupper, cfactor) )
      {
         _braid_TriSolve(core, level, iupper);
      }

      /* C-points (except for last iteration) */

      if (nu < nrelax)
      {
         /* Initiate communication (ignore F/C for now) and loop over center points */
         _braid_TriCommInit(core, level, &nrequests, &requests, &statuses, &buffers);
         for (i = (ilower+1); i <= (iupper-1); i++)
         {
            if ( _braid_IsCPoint(i, cfactor) )
            {
               _braid_TriSolve(core, level, i);
            }
         }
         /* Finalize communication and loop over end points */
         _braid_TriCommWait(core, level, nrequests, &requests, &statuses, &buffers);
         if ( _braid_IsCPoint(ilower, cfactor) )
         {
            _braid_TriSolve(core, level, ilower);
         }
         if ( (iupper > ilower) && _braid_IsCPoint(iupper, cfactor) )
         {
            _braid_TriSolve(core, level, iupper);
         }
      }
   }

   /* RDF: Should we keep this as a feature?  What to call it? */

   _braid_CoreElt(core, xrelax) = 1;

   /* Initiate communication (ignore F/C for now) and loop over center points */
   _braid_TriCommInit(core, level, &nrequests, &requests, &statuses, &buffers);
   for (i = (ilower+1); i <= (iupper-1); i++)
   {
      _braid_TriSolve(core, level, i);
   }
   /* Finalize communication and loop over end points */
   _braid_TriCommWait(core, level, nrequests, &requests, &statuses, &buffers);
   _braid_TriSolve(core, level, ilower);
   _braid_TriSolve(core, level, iupper);

   _braid_CoreElt(core, xrelax) = 0;

   return _braid_error_flag;
}

