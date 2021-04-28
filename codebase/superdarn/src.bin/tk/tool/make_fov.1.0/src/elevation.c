/* elevation.c
   ===========
   Author: S.G. Shepherd, Angeline G. Burrell - NRL - 2021
*/

/*
 LICENSE AND DISCLAIMER

 This file is part of the Radar Software Toolkit (RST).

 RST is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 any later version.

 RST is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with RST.  If not, see <http://www.gnu.org/licenses/>.
*/

/* TODO: add to rst/codebase/superdarn/src.lib/tk/elevation.1.0 */

#include <math.h>
#include <stdio.h>
#include "fitblk.h"
#include "rmath.h"

/**
 * @brief Calculate elevation for the specified field of view
 *
 * @param[in] lobe     Field-of-view specifier: 1=front, -1=back
 *            prm      The FitPrm struct holding rawacf record info
 *            psi_obs  Phase lag value
 *
 * @param[out] alpha   Elevation angle [deg]
 *
 * Note: SGS somehow need to pass in options for allowing negative elevation
 *       angles and residual phase
 **/

double elevation_v2_lobe(int lobe, struct FitPrm *prm, double psi_obs)
{
  static double X,Y,Z;      /* interferometer offsets [m]                    */
  double boff;              /* offset in beam widths to edge of FOV          */
  double phi0;              /* beam direction off boresight [rad]            */
  double cp0,sp0;           /* cosine and sine of phi0                       */
  double ca0,sa0;           /* cosine and sine of a0                         */
  double psi_ele;           /* phase delay due to electrical path diff [rad] */
  double psi_max;           /* maximum phase difference [rad]                */
  double a0;                /* angle where phase is maximum [rad]            */
  int    sgn;               /* sign of Y offset                              */
  double dpsi;              /* delta phase [rad]                             */
  double n2pi;              /* # of 2PI factors for correct mapping          */
  double d2pi;              /* correct multiple value of 2PIs                */
  double E;                 /* factor for simplifying expression             */
  double alpha;             /* elevation angle [degrees]                     */

  static double d = -9999.; /* separation of antenna arrays [m]              */

  /* At first call, calculate the values that don't change. */
  if (d < -999.)
    {
      X = prm->interfer[0];
      Y = prm->interfer[1];
      Z = prm->interfer[2];

      d = sqrt(X*X + Y*Y + Z*Z);
  }

  /* SGS: 20180926
   *
   * There is still some question as to exactly what the phidiff parameter in
   * the hdw.dat files means. The note in the hdw.dat files, presumably written
   * by Ray is:
   * 12) Phase sign (Cabling errors can lead to a 180 degree shift of the
   *     interferometry phase measurement. +1 indicates that the sign is
   *     correct, -1 indicates that it must be flipped.)
   * The _only_ hdw.dat file that has this value set to -1 is GBR during the
   * time period: 19870508 - 19921203
   *
   * To my knowlege there is no data available prior to 1993, so dealing with
   * this parameter is no longer necessary. For this reason I am simply
   * removing it from this algorithm.
   */

  sgn = (Y < 0) ? -1 : 1;

  boff = prm->maxbeam / 2.0 - 0.5;
  phi0 = prm->bmsep * (prm->bmnum - boff) * PI / 180.0;
  cp0  = cos(phi0);
  sp0  = sin(phi0);

  /* k = 2 * PI * prm->tfreq * 1000.0 / C;                                  */

  /* Phase delay [radians] due to electrical path difference.               *
   *   If the path length (cable and electronics) to the interferometer is  *
   *   shorter than that to the main antenna array, then the time for the   *
   *   to transit the interferometer electrical path is shorter: tdiff < 0  */
  psi_ele = -2.0 * PI * prm->tfreq * prm->tdiff * 1.0e-3;

  /* Determine the elevation angle (a0) where the phase difference (psi) is *
   * at its maximum.  This occurs when k and d are anti-parallel. Using     *
   * calculus of variations to compute the value: d(psi)/d(a) = 0           */
  a0 = asin(sgn * Z * cp0 / sqrt(Y*Y + Z*Z));

  /* Note: We are assuming that negative elevation angles are unphysical.   *
   *   The act of setting a0 = 0 _only_ has the effect to change psi_max    *
   *   (which is used to compute the correct number of 2pi factors and map  *
   *   the observed phase to the actual phase. The _only_ elevation angles  *
   *   that are affected are the small range from [-a0, 0]. Instead of      *
   *   these being mapped to negative elevation they are mapped to very     *
   *   small ranges just below the maximum.                                 */

  /* Note that it is possible in some cases with sloping ground that        *
   * extends far in front of the radar, that negative elevation angles      *
   * might exist. However, since elevation angles near the maximum "share"  *
   * this phase [-pi, pi] it is perhaps more likely that the higher         *
   * elevation angles are actually what is being observed.                  */

  /* In either case, one must decide which angle to chose (just as with all *
   * the aliased angles). Here we decide (unless the keyword 'negative' is  *
   * set) that negative elevation angles are unphysical and map them to the *
   * upper end.                                                             */

  if (a0 < 0) a0 = 0.0;  /* SGS and ~keyword_set(negative) */

  ca0 = cos(a0);
  sa0 = sin(a0);
  /* maximum phase = psi_ele + psi_geo(a0)                                  */
  psi_max = psi_ele + 2.0 * PI * prm->tfreq * 1.0e3 / C *
                     (X * sp0 + Y * sqrt(ca0*ca0 - sp0*sp0) + Z * sa0);

  /* Compute the number of 2pi factors necessary to map to correct region.  *
   * The lobe direction changes the sign of the observed phase difference,  *
   * phi_obs. (AGB)                                                         */
  dpsi = lobe * (psi_max - psi_obs);
  n2pi = (Y > 0) ? floor(dpsi / (2.0 * PI)) : ceil(dpsi / (2.0 * PI));
  d2pi = n2pi * 2.0 * PI;

  /* map observed phase to correct extended phase                           */
  psi_obs += d2pi;
  /* SGS: if not keyword_set(actual) then psi_obs += d2pi                   */

  /* Now solve for the elevation angle: alpha                               */
  E = (psi_obs / (2.0*PI*prm->tfreq*1.0e3) + prm->tdiff*1e-6) * C - X * sp0;
  alpha = asin((E*Z + sqrt(E*E * Z*Z - (Y*Y + Z*Z)*(E*E - Y*Y*cp0*cp0))) /
               (Y*Y + Z*Z));

  return (180.0 * alpha / PI);
}
