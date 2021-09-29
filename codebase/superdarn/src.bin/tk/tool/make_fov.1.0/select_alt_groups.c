/* select_alt_groups.c
   ===================
   Author: Angeline G. Burrell - NRL - 2021
   This is a U.S. government work and not under copyright protection in the U.S.

   This file is part of the Radar Software Toolkit (RST).

   Disclaimer: RST is licensed under GPL v3.0. Please visit 
               <https://www.gnu.org/licenses/> to see the full license

   Modifications:

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mpfit.h"

#include "fitmultbsid.h"
#include "stat_utils.h"
#include "sort.h"

/**
 * @brief Get the alt limits for a select group of data.  Look at the
 *        distribution of the data and fit a Gaussian curve to the occurance
 *        peaks to establish appropriate limits.
 *
 * @params[in] num      - Number of points in `rg` and `vh` arrays
 *             vh       - Array of virtual heights for each range gate
 *             vh_min   - Minimum allowable virtual height in km
 *             vh_max   - Maximum allowable virtual height in km
 *             vh_box   - Width of virtual height box in km
 *             min_pnts - Minimum number of points allowed in a box
 *             max_vbin - Maximum number of virtuaal height bins
 *
 * @params[out] vh_mins  - Lower virtual height limit of each bin
 *              vh_maxs  - Upper virtual height limit of each bin
 *              npeaks   - Number of virtual height bins
 **/

int select_alt_groups(int num, float vh[], float vh_min, float vh_max,
		      float vh_box, int min_pnts, int max_vbin, float *vh_mins,
		      float *vh_maxs)
{
  int i, j, nbin, nmax, status, npeaks, *hist_bins, *ismax, *valmax;
  int *argmax;

  float local_min, local_max, vmin, vmax, vlow, vhigh, hist_width;
  float *hist_edges, *vh_peaks;

  double *params;

  struct gauss_data *private;
  struct mp_config_struct *config;
  struct mp_result_struct *result;

  int sort_expand_boundaries(int num, int max_vbin, float local_min,
			     float local_max, float vh_min, float vh_max,
			     float vh_box, float vh_mins[], float vh_maxs[],
			     float *vh_peaks);

  /* Initialize the structures and variables */
  private = (struct gauss_data *)(malloc(sizeof(struct gauss_data)));
  memset(private, 0, sizeof(struct gauss_data));
  result = (struct mp_result_struct *)malloc(sizeof(struct mp_result_struct));
  memset(result, 0, sizeof(struct mp_result_struct));
  config = (struct mp_config_struct *)malloc(sizeof(struct mp_config_struct));
  memset(config, 0, sizeof(struct mp_config_struct));
  vh_peaks = (float *)calloc(max_vbin, sizeof(float));

  npeaks = 0;

  /* Create a histogram of the number of observations at each virtual height */
  nbin = (int)((vh_max - vh_min) / (vh_box * 0.25));
  if(nbin > 10) nbin = 10;
  if(nbin <= 0)
    {
      fprintf(stderr, "vheight range too small for a histogram analysis: ");
      fprintf(stderr, "%d = (%f - %f) / %f", nbin, vh_max, vh_min,
	      vh_box * 0.25);
      free(private);
      free(result);
      free(vh_peaks);
      exit(1);
    }

  hist_bins = (int *)calloc(nbin, sizeof(int));
  hist_edges = (float *)calloc(nbin, sizeof(float));
  histogram(num, vh, nbin, vh_min, vh_max, hist_bins, hist_edges);

  /* Find the maxima in the histogram */
  ismax = (int *)calloc(nbin, sizeof(int));
  nmax  = int_argrelmax(nbin, hist_bins, 2, 1, ismax);

  /* A relative maximum can't be identified if two identical values */
  /* are side-by-side, and it should almost always be included.     */
  i = int_argabsmax(nbin, hist_bins);

  /* Only add the absolute maximum if it is significant and absent. */
  if(ismax[i] == 0 && hist_bins[nmax] >= min_pnts)
    {
      ismax[nmax] = 1;
      nmax++;
    }

  /* Get the maximum and minimum of the virtual heights */
  local_min = float_absmin(num, vh);
  local_max = float_absmax(num, vh);

  /* Without a significant maximum, set the limits using the suggested width */
  if(nmax == 0)
    {
      npeaks = (int)ceil((double)(local_max - local_min) / (double)vh_box);
      vmin   = (local_max - local_min) / (float)npeaks + local_min - vh_box;

      if(npeaks > max_vbin)
	{
	  fprintf(stderr, "suggested width created too many vheight bins\n");
	  exit(1);
	}

      for(i = 0; i < npeaks; i++)
	{
	  vh_mins[i]  = vmin + i * vh_box;
	  vh_maxs[i]  = vh_mins[i] + vh_box;
	  vh_peaks[i] = vmin + 0.5 * vh_box;
	}
    }
  else
    {
      /* Configure the least-squares fitting structure, setting */
      /* variables not assigned to the defaults                 */
      config->maxfev = 1600;

      /* Now set the defaults */
      config->ftol          = 1.0e-10;
      config->xtol          = 1.0e-10;
      config->gtol          = 1.0e-10;
      config->epsfcn        = MP_MACHEP0;
      config->stepfactor    = 100.0;
      config->covtol        = 1.0e-14;
      config->maxiter       = 200;
      config->nprint        = 1;
      config->douserscale   = 0;
      config->nofinitecheck = 0;      
      config->iterproc      = 0;

      /* Sort the maxima */
      valmax = (int *)malloc(nmax * sizeof(int));
      argmax = (int *)calloc(nmax, sizeof(int));
      for(i = 0, j = 0; i < nbin && j < nmax; i++)
	{
       	  if(ismax[i] == 1)
	    {
	      argmax[j]   = i;
	      valmax[j++] = hist_bins[i];
	    }
	}

      /* Cast the histogram bins as doubles for use by MINPACK */
      private->x       = (double *)calloc(nbin, sizeof(double));
      private->y       = (double *)calloc(nbin, sizeof(double));
      private->y_error = (double *)calloc(nbin, sizeof(double));

      hist_width = (nbin >= 1) ? (hist_edges[1] - hist_edges[0]) / 2.0 : vh_box;

      /* Set the structure for values that will not be updated */
      for(i = 0; i < nbin; i++)
      	{
      	  private->x[i]       = (double)(hist_edges[i] + hist_width);
	  private->y[i]       = (double)hist_bins[i]; 
      	  private->y_error[i] = 1.0;  /* Unity is the same as no error */
      	}

      /* Set the parameters */
      params = (double *)malloc(sizeof(double) * (nmax * 3 + 1));
      params[0] = (double)nmax;

      for(j = 0; j < nmax; j++)
	{
	  params[1 + j * 3] = (double)valmax[j];
	  params[2 + j * 3] = private->x[argmax[j]];
	  params[3 + j * 3] = 0.5 * (double)vh_box;
	}

      /* Use non-linear least-squares to fit a Gaussian to the */
      /* histogram.                                            */
      status = mpfit(mult_gaussian_dev, nbin, 3, params, 0, config, private, result);

      /* Accept all success conditions */
      if(status > 1)
	{
	  for(j = 0; j < nmax; j++)
	    {
	      /* Get the 3-sigma limits */
	      vmin = params[2 + j * 3] - 3.0 * params[3 + j * 3];
	      if(vmin < vh_min) vmin = vh_min;

	      vmax = params[2 + j * 3] + 3.0 * params[3 + j * 3];
	      if(vmax > vh_max) vmax = vh_max;
    
	      /* Get the 2-sigma limits */
	      vlow = params[2 + j * 3] - 2.0 * params[3 + j * 3];
	      if(vlow < vh_min) vlow = vh_min;

	      vhigh = params[2 + j * 3] + 2.0 * params[3 + j * 3];
	      if(vhigh > vh_max) vhigh = vh_max;

	      /* Save the 3-sigma limits as the upper and lower virtual */
	      /* height box limits if the detected peak is within the   */
	      /* 2-sigma limits.  Also remove this peak from the xdata  */
	      /* array to allow lower level peaks to be identified.     */
	      if((private->x[argmax[j]] >= vlow) && (private->x[argmax[j]] <= vhigh))
		{
		  /* Make sure there is enough memory for this peak */
		  if(npeaks >= max_vbin)
		    {
		      fprintf(stderr,
			      "histogram fits created too many vheight bins\n");
		      exit(1);
		    }

		  /* Save this altitude bin */
		  vh_mins[npeaks]  = vmin;
		  vh_maxs[npeaks]  = vmax;
		  vh_peaks[npeaks] = params[1];
		  npeaks++;
		}
	    }

	  free(valmax);
	  free(argmax);
	  free(params);
	}

      /* Evaluate the current limits to see if the overlap each or have gaps. */
      /* Use the suggested width to set limits if done were found.            */
      if(npeaks == 0)
	{
	  /* Get the expected number of peaks and set the first set of */
	  /* boundary limits.                                          */
	  j           = (int)ceil((double)((local_max - local_min) / vh_box));
	  vh_mins[0]  = (local_max
			- local_min) / (float)j + local_min - vh_box;
	  if(vh_mins[0] < vh_min) vh_mins[0] = vh_min;
	  vh_maxs[0]  = vh_mins[0] + vh_box;
	  if(vh_maxs[0] > vh_max) vh_maxs[0] = vh_max;
	  vh_peaks[i] = 0.5 * (vh_maxs[i] - vh_mins[i]) + vh_mins[i];

	  /* Set each limit, stopping if the maximum height is reached */
	  for(i = 1; i < j && vh_maxs[i-1] < vh_max; i++)
	    {
	      vh_mins[i] = vh_maxs[i - 1];
	      vh_maxs[i] = vh_mins[i] + vh_box;
	      if(vh_maxs[i] > vh_max) vh_maxs[i] = vh_max;
	      vh_peaks[i] = 0.5 * (vh_maxs[i] - vh_mins[i]) + vh_mins[i];
	    }
	  npeaks = i;
	}
      else
	{
	  /* Sorts the virtual height limits, eliminating overlaps and gaps */
	  npeaks = sort_expand_boundaries(npeaks, max_vbin, local_min,
					  local_max, vh_min, vh_max, vh_box,
					  vh_mins, vh_maxs, vh_peaks);
	}

      /* Free the allocated sub-pointers */
      free(private->x);
      free(private->y);
      free(private->y_error);
      free(result->resid);
      free(result->xerror);
      free(result->covar);
    }

  /* Free the allocated memory */
  free(hist_bins);
  free(hist_edges);
  free(vh_peaks);
  free(private);
  free(result);
  free(config);

  /* Return the number of virtual height bins as the routine status */
  return(npeaks);
}

/**
 * @brief Sorts the virtual height limits, eliminating overlaps and gaps
 *
 * @params[in] num       - Number of values in vh_(mins/maxs/peaks) at input
 *             local_min - Minimum of provided virtual height values in km
 *             local_max - Maximum of provided virtual height values in km
 *             vh_box    - Width of desired virtual height bin in km
 *
 * @params[in/out] vh_mins  - Lower limit of virtual height bins in km
 *                 vh_maxs  - Upper limit of virtual height bins in km
 *                 vh_peaks - Peak height for virtual height bins in km
 *
 * @reference Part of davitpy.proc.fov.update_backscatter.select_alt_groups
 **/

int sort_expand_boundaries(int num, int max_vbin, float local_min,
			   float local_max, float vh_min, float vh_max,
			   float vh_box, float vh_mins[], float vh_maxs[],
			   float *vh_peaks)
{
  int i, inew, vnum, *sortargs, *priority;

  float hmin, vspan, *new_mins, *new_maxs, *new_peaks;

  void smart_argsort_float(int num, float array[], int sortargs[]);

  if(num == 0) return(0);

  /* Initialize the local pointers */
  new_mins  = (float *)calloc(max_vbin, sizeof(float));
  new_maxs  = (float *)calloc(max_vbin, sizeof(float));
  new_peaks = (float *)calloc(max_vbin, sizeof(float));
  priority  = (int *)calloc(max_vbin, sizeof(int));
  inew      = 0;

  /* Get the indices for sorted Gaussian limits */
  sortargs = (int *)calloc(num, sizeof(int));
  smart_argsort_float(num, vh_mins, sortargs);

  /* If there are points that fall below the lower limit, add more regions */
  /* using the suggested width limits.                                     */
  if(vh_mins[sortargs[0]] > local_min)
    {
      vnum = (int)((vh_mins[sortargs[0]] - local_min) / vh_box);
      if(vnum == 0)
	{
	  /* The outlying points are close enough that the lower limit */
	  /* should be extended.                                       */
	  vh_mins[sortargs[0]] = floor(local_min);
	  if(vh_mins[sortargs[0]] < vh_min)
	    vh_mins[sortargs[0]] = vh_min;
	}
      else
	{
	  /* Create new virtual height bins and prioritize them. Low      */
	  /* priority values indicate a higher priority to keep this bin. */
	  vspan = (vh_mins[sortargs[0]] - local_min) / (float)vnum;

	  for(i = 0; i < vnum; i++)
	    {
	      /* Calculate the lower limit of the virtual height bin */
	      hmin = local_min + (float)i * vspan;
	      if(hmin < local_min) hmin = local_min;

	      /* Add the new virtual height bin to the start of the local */
	      /* pointers, which need to be sorted from least to greatest */
	      new_mins[inew]  = floor(hmin);
	      new_maxs[inew]  = ceil(hmin + vspan);
	      new_peaks[inew] = hmin + 0.5 * vspan;
	      priority[inew]  = inew + num;
	      inew++;

	      if(inew >= max_vbin)
		{
		  fprintf(stderr,
			  "exceeded new virtual height boundary limits\n");
		  exit(1);
		}
	    }
	}
    }

  /* Add the Gaussian limits to the local pointers */
  for(i = 0; i < num; i++)
    {
      /* Get the next lowest minimum virtual height bin value */
      hmin = vh_mins[sortargs[i]];

      /* Test for overlapping bins and gaps */
      if(inew > 0)
	{
	  /* Test for overlaps or gaps with the previous height window */
	  if((new_maxs[inew - 1] >= vh_peaks[sortargs[i]])
	     || (hmin <= new_peaks[inew - 1]))
	    {
	      /* There is a significant overlap between the two regions. */
	      /* Use the priority to decide which boundary to adjust.   */
	      if(priority[inew - 1] < sortargs[i])
		{
		  new_mins[inew]  = new_maxs[inew - 1];
		  new_maxs[inew]  = ceil(vh_maxs[sortargs[i]]);
		  new_peaks[inew] = new_mins[inew] + 0.5 * (new_maxs[inew] - new_mins[inew]);
		  priority[inew]  = inew + num;
		  inew++;
		}
	      else
		{
		  /* If this adjustment places the previous maximum at or */
		  /* below the previous minimum, remove that height bin.  */
		  while(hmin <= new_mins[inew - 1] && inew > 0)
		    inew--;

		  /* Set the maximum of the new last window to the minimum */
		  /* of the next window, removing any gap.                 */
		  if(inew > 0) new_maxs[inew - 1] = hmin;
		}
	    }
	  else if(new_maxs[inew - 1] < hmin)
	    {
	      /* There is a gap between the two height bins. Construct */
	      /* bridging window(s) before adding the current height   */
	      /* bin to the local pointers.                            */
	      vnum = (int)((hmin - new_maxs[inew - 1]) / vh_box);

	      if(vnum == 0)
		{
		  /* The outlying points are close enough that the last */
		  /* upper limit should be expanded.                    */
		  new_maxs[inew - 1] = hmin;
		}
	      else
		{
		  vspan = (hmin - new_maxs[inew - 1]) / (float)vnum;

		  for(i = 0; i < vnum; i++)
		    {
		      /* Add the new virtual height bin to the local pointers */
		      new_mins[inew]  = new_maxs[inew - 1];
		      new_maxs[inew]  = ceil(new_mins[inew] + vspan);
		      new_peaks[inew] = new_mins[inew] + 0.5 * vspan;
		      priority[inew]  = inew + num;
		      inew++;
		    }
		}
	    }
	  else
	    {
	      /* Add the current height bin, if it is a sensible width */
	      if(hmin < vh_maxs[sortargs[i]])
		{
		  new_mins[inew]  = hmin;
		  new_maxs[inew]  = ceil(vh_maxs[sortargs[i]]);
		  new_peaks[inew] = vh_peaks[sortargs[i]];
		  priority[inew]  = sortargs[i];
		  inew++;
		}
	    }
	}
      else
	{
	  /* Add the current height bin, if it is a sensible width */
	  if(hmin < vh_maxs[sortargs[i]])
	    {
	      new_mins[inew]  = floor(hmin);
	      new_maxs[inew]  = ceil(vh_maxs[sortargs[i]]);
	      new_peaks[inew] = vh_peaks[sortargs[i]];
	      priority[inew]  = sortargs[i];
	      inew++;
	    }
	}

      if(inew >= max_vbin)
	{
	  fprintf(stderr, "exceeded new virtual height boundary limits\n");
	  exit(1);
	}
    }

  /* If there are points that fall above the upper limit, add more regions */
  if((num == 0) || (new_maxs[inew - 1] < local_max))
    {
      vnum = (int)((local_max - new_maxs[inew - 1]) / vh_box);

      if(vnum == 0)
	{
	  /* The outlying points are close enough that the upper limit */
	  /* should be expanded.                                       */
	  new_maxs[inew - 1] = ceil(local_max);
	  if(new_maxs[inew - 1] > vh_max) new_maxs[inew - 1] = vh_max;
	}
      else
	{
	  vspan = (local_max - new_maxs[inew - 1]) / (float)vnum;

	  for(i = 0; i < vnum && new_maxs[inew - 1] < vh_max; i++)
	    {
	      /* Get the maximum, ensuring it doesn't extend too high */
	      hmin = new_maxs[inew - 1] + vspan;
	      if(hmin > vh_max) hmin = vh_max;
	      
	      /* Add the new virtual height bin to the local pointers */
	      new_mins[inew]  = new_maxs[inew - 1];
	      new_maxs[inew]  = ceil(hmin);
	      new_peaks[inew] = 0.5 * (hmin - new_mins[inew]) + new_mins[inew];
	      priority[inew]  = inew + num;
	      inew++;
	    }
	}
    }

  /* Update the output with the sorted local values */
  for(i = 0; i < inew; i++)
    {
      vh_mins[i]  = new_mins[i];
      vh_maxs[i]  = new_maxs[i];
      vh_peaks[i] = new_peaks[i];
    }

  /* Free the local pointers */
  free(sortargs);
  free(new_mins);
  free(new_maxs);
  free(new_peaks);
  free(priority);

  return(inew);
}
