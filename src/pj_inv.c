/******************************************************************************
 * Project:  PROJ.4
 * Purpose:  Inverse operation invocation
 * Author:   Thomas Knudsen,  thokn@sdfe.dk,  2018-01-02
 *           Based on material from Gerald Evenden (original pj_inv)
 *           and Piyush Agram (original pj_inv3d)
 *
 ******************************************************************************
 * Copyright (c) 2000, Frank Warmerdam
 * Copyright (c) 2018, Thomas Knudsen / SDFE
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/
#include <errno.h>

#include "proj_internal.h"
#include "projects.h"

#define INPUT_UNITS  P->right
#define OUTPUT_UNITS P->left

static PJ_COORD pj_inv_prepare (PJ *P, PJ_COORD coo) {
    if (coo.xyz.x == HUGE_VAL) {
        proj_errno_set (P, PJD_ERR_INVALID_X_OR_Y);
        return proj_coord_error ();
    }

    /* The helmert datum shift will choke unless it gets a sensible 4D coordinate */
    if (HUGE_VAL==coo.v[2] && P->helmert) coo.v[2] = 0.0;
    if (HUGE_VAL==coo.v[3] && P->helmert) coo.v[3] = 0.0;

    if (P->axisswap)
        coo = proj_trans (P->axisswap, PJ_INV, coo);

    /* Check validity of angular input coordinates */
    if (INPUT_UNITS==PJ_IO_UNITS_ANGULAR) {
        double t;

        /* check for latitude or longitude over-range */
        t = (coo.lp.phi < 0  ?  -coo.lp.phi  :  coo.lp.phi) - M_HALFPI;
        if (t > PJ_EPS_LAT  ||  coo.lp.lam > 10  ||  coo.lp.lam < -10) {
            proj_errno_set (P, PJD_ERR_LAT_OR_LON_EXCEED_LIMIT);
            return proj_coord_error ();
        }

        /* Clamp latitude to -90..90 degree range */
        if (coo.lp.phi > M_HALFPI)
            coo.lp.phi = M_HALFPI;
        if (coo.lp.phi < -M_HALFPI)
            coo.lp.phi = -M_HALFPI;

        /* If input latitude is geocentrical, convert to geographical */
        if (P->geoc)
            coo = proj_geocentric_latitude (P, PJ_INV, coo);

        /* Distance from central meridian, taking system zero meridian into account */
        coo.lp.lam = (coo.lp.lam + P->from_greenwich) - P->lam0;

        /* Ensure longitude is in the -pi:pi range */
        if (0==P->over)
            coo.lp.lam = adjlon(coo.lp.lam);

        if (P->hgridshift)
            coo = proj_trans (P->hgridshift, PJ_FWD, coo);
        else if (P->helmert) {
            coo = proj_trans (P->cart,       PJ_FWD, coo); /* Go cartesian in local frame */
            coo = proj_trans (P->helmert,    PJ_FWD, coo); /* Step into WGS84 */
            coo = proj_trans (P->cart_wgs84, PJ_INV, coo); /* Go back to angular using WGS84 ellps */
        }
        if (coo.lp.lam==HUGE_VAL)
            return coo;
        if (P->vgridshift)
            coo = proj_trans (P->vgridshift, PJ_INV, coo); /* Go geometric from orthometric */
        return coo;
    }

    /* Handle remaining possible input types */
    switch (INPUT_UNITS) {
    case PJ_IO_UNITS_WHATEVER:
        return coo;

    /* de-scale and de-offset */
    case PJ_IO_UNITS_CARTESIAN:
        coo.xyz.x = P->to_meter * coo.xyz.x - P->x0;
        coo.xyz.y = P->to_meter * coo.xyz.y - P->y0;
        coo.xyz.z = P->to_meter * coo.xyz.z - P->z0;

        if (P->is_geocent)
            coo = proj_trans (P->cart, PJ_INV, coo);

        return coo;

    case PJ_IO_UNITS_PROJECTED:
    case PJ_IO_UNITS_CLASSIC:
        coo.xyz.x = P->to_meter  * coo.xyz.x - P->x0;
        coo.xyz.y = P->to_meter  * coo.xyz.y - P->y0;
        coo.xyz.z = P->vto_meter * coo.xyz.z - P->z0;
        if (INPUT_UNITS==PJ_IO_UNITS_PROJECTED)
            return coo;

        /* Classic proj.4 functions expect plane coordinates in units of the semimajor axis  */
        /* Multiplying by ra, rather than dividing by a because the CalCOFI projection       */
        /* stomps on a and hence (apparently) depends on this to roundtrip correctly         */
        /* (CalCOFI avoids further scaling by stomping - but a better solution is possible)  */
        coo.xyz.x *= P->ra;
        coo.xyz.y *= P->ra;
        return coo;
    /* Silence some compiler warnings about PJ_IO_UNITS_ANGULAR not handled */
    default:
        break;
    }

    /* Should not happen, so we could return pj_coord_err here */
    return coo;
}



static PJ_COORD pj_inv_finalize (PJ *P, PJ_COORD coo) {
    if (coo.xyz.x == HUGE_VAL) {
        proj_errno_set (P, PJD_ERR_INVALID_X_OR_Y);
        return proj_coord_error ();
    }

    if (OUTPUT_UNITS==PJ_IO_UNITS_ANGULAR) {

        if (INPUT_UNITS!=PJ_IO_UNITS_ANGULAR) {
            /* Distance from central meridian, taking system zero meridian into account */
            coo.lp.lam = coo.lp.lam + P->from_greenwich + P->lam0;

            /* adjust longitude to central meridian */
            if (0==P->over)
                coo.lpz.lam = adjlon(coo.lpz.lam);

            if (P->vgridshift)
                coo = proj_trans (P->vgridshift, PJ_INV, coo); /* Go geometric from orthometric */
            if (coo.lp.lam==HUGE_VAL)
                return coo;
            if (P->hgridshift)
                coo = proj_trans (P->hgridshift, PJ_FWD, coo);
            else if (P->helmert) {
                coo = proj_trans (P->cart,       PJ_FWD, coo); /* Go cartesian in local frame */
                coo = proj_trans (P->helmert,    PJ_FWD, coo); /* Step into WGS84 */
                coo = proj_trans (P->cart_wgs84, PJ_INV, coo); /* Go back to angular using WGS84 ellps */
            }
            if (coo.lp.lam==HUGE_VAL)
                return coo;
        }

        /* If input latitude was geocentrical, convert back to geocentrical */
        if (P->geoc)
            coo = proj_geocentric_latitude (P, PJ_FWD, coo);
    }

    return coo;
}



LP pj_inv(XY xy, PJ *P) {
    PJ_COORD coo = {{0,0,0,0}};
    coo.xy = xy;

    if (!P->skip_inv_prepare)
        coo = pj_inv_prepare (P, coo);
    if (HUGE_VAL==coo.v[0])
        return proj_coord_error ().lp;

    /* Do the transformation, using the lowest dimensional transformer available */
    if (P->inv)
        coo.lp = P->inv(coo.xy, P);
    else if (P->inv3d)
        coo.lpz = P->inv3d (coo.xyz, P);
    else if (P->inv4d)
        coo = P->inv4d (coo, P);
    else {
        proj_errno_set (P, EINVAL);
        return proj_coord_error ().lp;
    }
    if (HUGE_VAL==coo.v[0])
        return proj_coord_error ().lp;

    if (!P->skip_inv_finalize)
        coo = pj_inv_finalize (P, coo);
    return coo.lp;
}



LPZ pj_inv3d (XYZ xyz, PJ *P) {
    PJ_COORD coo = {{0,0,0,0}};
    coo.xyz = xyz;

    if (!P->skip_inv_prepare)
        coo = pj_inv_prepare (P, coo);
    if (HUGE_VAL==coo.v[0])
        return proj_coord_error ().lpz;

    /* Do the transformation, using the lowest dimensional transformer feasible */
    if (P->inv3d)
        coo.lpz = P->inv3d (coo.xyz, P);
    else if (P->inv4d)
        coo = P->inv4d (coo, P);
    else if (P->inv)
        coo.lp = P->inv (coo.xy, P);
    else {
        proj_errno_set (P, EINVAL);
        return proj_coord_error ().lpz;
    }
    if (HUGE_VAL==coo.v[0])
        return proj_coord_error ().lpz;

    if (!P->skip_inv_finalize)
        coo = pj_inv_finalize (P, coo);
    return coo.lpz;
}



PJ_COORD pj_inv4d (PJ_COORD coo, PJ *P) {
    if (!P->skip_inv_prepare)
        coo = pj_inv_prepare (P, coo);
    if (HUGE_VAL==coo.v[0])
        return proj_coord_error ();

    /* Call the highest dimensional converter available */
    if (P->inv4d)
        coo = P->inv4d (coo, P);
    else if (P->inv3d)
        coo.lpz = P->inv3d (coo.xyz, P);
    else if (P->inv)
        coo.lp = P->inv (coo.xy, P);
    else {
        proj_errno_set (P, EINVAL);
        return proj_coord_error ();
    }
    if (HUGE_VAL==coo.v[0])
        return proj_coord_error ();

    if (!P->skip_inv_finalize)
        coo = pj_inv_finalize (P, coo);
    return coo;
}
