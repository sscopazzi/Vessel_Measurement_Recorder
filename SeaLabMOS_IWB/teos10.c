/* TEOS-10 subset, copied verbatim from GSW-C (github.com/TEOS-10/GSW-C, GSW licence: see LICENSES/LicenseRef-GSW.txt; redistributed unmodified).
   Only the functions the SeaLabMOS IWB and the WTB need: practical salinity from conductivity,
   Reference Salinity, Conservative Temperature, in-situ density and depth from pressure. */
#include "teos10.h"
#include <math.h>
#define GSW_INVALID_VALUE 9e15
#include "gsw_internal_const.h"

double
gsw_hill_ratio_at_sp2(double t)
{
        GSW_SP_COEFFICIENTS;
        double  g0 = 2.641463563366498e-1, g1 = 2.007883247811176e-4,
                g2 = -4.107694432853053e-6, g3 = 8.401670882091225e-8,
                g4 = -1.711392021989210e-9, g5 = 3.374193893377380e-11,
                g6 = -5.923731174730784e-13, g7 = 8.057771569962299e-15,
                g8 = -7.054313817447962e-17, g9 = 2.859992717347235e-19,
                sp2 = 2.0;
        double  t68, ft68, rtx0, dsp_drtx, sp_est, rtx, rtxm, x, part1, part2;
        double  sqrty, sp_hill_raw_at_sp2;

        t68     = t*1.00024;
        ft68    = (t68 - 15.0)/(1.0 + k*(t68 - 15.0));

    /*!------------------------------------------------------------------------
    **! Find the initial estimates of Rtx (Rtx0) and of the derivative dSP_dRtx
    **! at SP = 2.
    **!------------------------------------------------------------------------
    */
        rtx0    = g0 + t68*(g1 + t68*(g2 + t68*(g3 + t68*(g4 + t68*(g5
                + t68*(g6 + t68*(g7 + t68*(g8 + t68*g9))))))));

        dsp_drtx= a1 + (2*a2 + (3*a3 + (4*a4 + 5*a5*rtx0)*rtx0)*rtx0)*rtx0 +
                ft68*(b1 + (2*b2 + (3*b3 + (4*b4 + 5*b5*rtx0)*rtx0)*rtx0)*rtx0);

    /*!-------------------------------------------------------------------------
    **! Begin a single modified Newton-Raphson iteration to find Rt at SP = 2.
    **!-------------------------------------------------------------------------
    */
        sp_est  = a0 + (a1 + (a2 + (a3 + (a4 + a5*rtx0)*rtx0)*rtx0)*rtx0)*rtx0
                + ft68*(b0 + (b1 + (b2+ (b3 + (b4 + b5*rtx0)*rtx0)*rtx0)*
                  rtx0)*rtx0);
        rtx     = rtx0 - (sp_est - sp2)/dsp_drtx;
        rtxm    = 0.5*(rtx + rtx0);
        dsp_drtx= a1 + (2*a2 + (3*a3 + (4*a4 + 5*a5*rtxm)*rtxm)*rtxm)*rtxm
                + ft68*(b1 + (2*b2 + (3*b3 + (4*b4 + 5*b5*rtxm)*
                                                rtxm)*rtxm)*rtxm);
        rtx     = rtx0 - (sp_est - sp2)/dsp_drtx;
    /*
    **! This is the end of one full iteration of the modified Newton-Raphson
    **! iterative equation solver. The error in Rtx at this point is equivalent
    **! to an error in SP of 9e-16 psu.
    */

        x       = 400.0*rtx*rtx;
        sqrty   = 10.0*rtx;
        part1   = 1.0 + x*(1.5 + x);
        part2   = 1.0 + sqrty*(1.0 + sqrty*(1.0 + sqrty));
        sp_hill_raw_at_sp2 = sp2 - a0/part1 - b0*ft68/part2;

        return (2.0/sp_hill_raw_at_sp2);
}

double
gsw_sp_from_c(double c, double t, double p)
{
        GSW_TEOS10_CONSTANTS;
        GSW_SP_COEFFICIENTS;
        double  sp, t68, ft68, r, rt_lc, rp, rt, rtx,
                hill_ratio, x, sqrty, part1, part2,
                sp_hill_raw;

        t68     = t*1.00024e0;
        ft68    = (t68 - 15e0)/(1e0 + k*(t68 - 15e0));
    /*
     ! The dimensionless conductivity ratio, R, is the conductivity input, C,
     ! divided by the present estimate of C(SP=35, t_68=15, p=0) which is
     ! 42.9140 mS/cm (=4.29140 S/m), (Culkin and Smith, 1980).
    */

        r = c/gsw_c3515;        /* 0.023302418791070513 = 1./42.9140 */

        /*rt_lc corresponds to rt as defined in the UNESCO 44 (1983) routines.*/
        rt_lc   = c0 + (c1 + (c2 + (c3 + c4*t68)*t68)*t68)*t68;
        rp      = 1e0 + (p*(e1 + e2*p + e3*p*p))/(1e0 + d1*t68 + d2*t68*t68 +
                  (d3 + d4*t68)*r);
        rt      = r/(rp*rt_lc);

        if (rt < 0.0) {
            return (GSW_INVALID_VALUE);
        }

        rtx     = sqrt(rt);

        sp      = a0 + (a1 + (a2 + (a3 + (a4 + a5*rtx)*rtx)*rtx)*rtx)*rtx +
                  ft68*(b0 + (b1 + (b2 + (b3 +
                        (b4 + b5*rtx)*rtx)*rtx)*rtx)*rtx);
    /*
     ! The following section of the code is designed for SP < 2 based on the
     ! Hill et al. (1986) algorithm.  This algorithm is adjusted so that it is
     ! exactly equal to the PSS-78 algorithm at SP = 2.
    */

        if (sp < 2) {
            hill_ratio  = gsw_hill_ratio_at_sp2(t);
            x           = 400e0*rt;
            sqrty       = 10e0*rtx;
            part1       = 1e0 + x*(1.5e0 + x);
            part2       = 1e0 + sqrty*(1e0 + sqrty*(1e0 + sqrty));
            sp_hill_raw = sp - a0/part1 - b0*ft68/part2;
            sp          = hill_ratio*sp_hill_raw;
        }

    /* This line ensures that SP is non-negative. */
        if (sp < 0.0) {
            sp  = GSW_INVALID_VALUE;
        }

        return (sp);
}

double
gsw_sr_from_sp(double sp)
{
        GSW_TEOS10_CONSTANTS;

        return (sp*gsw_ups);
}

double
gsw_entropy_part(double sa, double t, double p)
{
        GSW_TEOS10_CONSTANTS;
        double  x2, x, y, z, g03, g08;

        x2      = gsw_sfac*sa;
        x       = sqrt(x2);
        y       = t*0.025;
        z       = p*1e-4;

        g03     = z*(-270.983805184062 +
                z*(776.153611613101 + z*(-196.51255088122 +
                   (28.9796526294175 - 2.13290083518327*z)*z))) +
                y*(-24715.571866078 + z*(2910.0729080936 +
                z*(-1513.116771538718 + z*(546.959324647056 +
                   z*(-111.1208127634436 + 8.68841343834394*z)))) +
                y*(2210.2236124548363 + z*(-2017.52334943521 +
                z*(1498.081172457456 + z*(-718.6359919632359 +
                   (146.4037555781616 - 4.9892131862671505*z)*z))) +
                y*(-592.743745734632 + z*(1591.873781627888 +
                z*(-1207.261522487504 + (608.785486935364 -
                   105.4993508931208*z)*z)) +
                y*(290.12956292128547 + z*(-973.091553087975 +
                z*(602.603274510125 + z*(-276.361526170076 +
                   32.40953340386105*z))) +
                y*(-113.90630790850321 + y*(21.35571525415769 -
                   67.41756835751434*z) +
                z*(381.06836198507096 + z*(-133.7383902842754 +
                   49.023632509086724*z)))))));

        g08     = x2*(z*(729.116529735046 +
                z*(-343.956902961561 + z*(124.687671116248 +
                   z*(-31.656964386073 + 7.04658803315449*z)))) +
                x*( x*(y*(-137.1145018408982 + y*(148.10030845687618 +
                   y*(-68.5590309679152 + 12.4848504784754*y))) -
                22.6683558512829*z) + z*(-175.292041186547 +
                   (83.1923927801819 - 29.483064349429*z)*z) +
                y*(-86.1329351956084 + z*(766.116132004952 +
                   z*(-108.3834525034224 + 51.2796974779828*z)) +
                y*(-30.0682112585625 - 1380.9597954037708*z +
                   y*(3.50240264723578 + 938.26075044542*z)))) +
                y*(1760.062705994408 + y*(-675.802947790203 +
                y*(365.7041791005036 + y*(-108.30162043765552 +
                   12.78101825083098*y) +
                z*(-1190.914967948748 + (298.904564555024 -
                   145.9491676006352*z)*z)) +
                z*(2082.7344423998043 + z*(-614.668925894709 +
                   (340.685093521782 - 33.3848202979239*z)*z))) +
                z*(-1721.528607567954 + z*(674.819060538734 +
                z*(-356.629112415276 + (88.4080716616 -
                   15.84003094423364*z)*z)))));

        return (-(g03 + g08)*0.025);
}

double
gsw_entropy_part_zerop(double sa, double pt0)
{
        GSW_TEOS10_CONSTANTS;
        double  x2, x, y, g03, g08;

        x2      = gsw_sfac*sa;
        x       = sqrt(x2);
        y       = pt0*0.025;

        g03     = y*(-24715.571866078 + y*(2210.2236124548363 +
                y*(-592.743745734632 + y*(290.12956292128547 +
                y*(-113.90630790850321 + y*21.35571525415769)))));

        g08     = x2*(x*(x*(y*(-137.1145018408982 + y*(148.10030845687618 +
                y*(-68.5590309679152 + 12.4848504784754*y)))) +
                y*(-86.1329351956084 + y*(-30.0682112585625 +
                   y*3.50240264723578))) +
                y*(1760.062705994408 + y*(-675.802947790203 +
                y*(365.7041791005036 + y*(-108.30162043765552 +
                   12.78101825083098*y)))));

        return (-(g03 + g08)*0.025);
}

double
gsw_gibbs_pt0_pt0(double sa, double pt0)
{
        GSW_TEOS10_CONSTANTS;
        double  x2, x, y, g03, g08;

        x2      = gsw_sfac*sa;
        x       = sqrt(x2);
        y       = pt0*0.025;

        g03     = -24715.571866078 +
                y*(4420.4472249096725 +
                y*(-1778.231237203896 +
                y*(1160.5182516851419 +
                y*(-569.531539542516 + y*128.13429152494615))));

        g08     = x2*(1760.062705994408 + x*(-86.1329351956084 +
                x*(-137.1145018408982 + y*(296.20061691375236 +
                y*(-205.67709290374563 + 49.9394019139016*y))) +
                y*(-60.136422517125 + y*10.50720794170734)) +
                y*(-1351.605895580406 + y*(1097.1125373015109 +
                y*(-433.20648175062206 + 63.905091254154904*y))));

        return ((g03 + g08)*0.000625);
}

double
gsw_pt0_from_t(double sa, double t, double p)
{
        GSW_TEOS10_CONSTANTS;
        int     no_iter;
        double  pt0, pt0_old, dentropy, dentropy_dt;
        double  s1, true_entropy_part, pt0m;

        s1      = sa/gsw_ups;

        pt0     = t+p*( 8.65483913395442e-6  -
                  s1 *  1.41636299744881e-6  -
                   p *  7.38286467135737e-9  +
                   t *(-8.38241357039698e-6  +
                  s1 *  2.83933368585534e-8  +
                   t *  1.77803965218656e-8  +
                   p *  1.71155619208233e-10));

        dentropy_dt     = gsw_cp0/((gsw_t0+pt0)*(1.0-0.05*(1.0-sa/gsw_sso)));

        true_entropy_part = gsw_entropy_part(sa,t,p);

        for (no_iter=1; no_iter <= 2; no_iter++) {
            pt0_old     = pt0;
            dentropy    = gsw_entropy_part_zerop(sa,pt0_old) -
                          true_entropy_part;
            pt0         = pt0_old - dentropy/dentropy_dt;
            pt0m        = 0.5*(pt0 + pt0_old);
            dentropy_dt = -gsw_gibbs_pt0_pt0(sa,pt0m);
            pt0         = pt0_old - dentropy/dentropy_dt;
        }
        return (pt0);
}

double
gsw_ct_from_pt(double sa, double pt)
{
        GSW_TEOS10_CONSTANTS;
        double  x2, x, y, pot_enthalpy;

        x2              = gsw_sfac*sa;
        x               = sqrt(x2);
        y               = pt*0.025e0;   /*! normalize for F03 and F08 */
        pot_enthalpy    =  61.01362420681071e0 + y*(168776.46138048015e0 +
             y*(-2735.2785605119625e0 + y*(2574.2164453821433e0 +
             y*(-1536.6644434977543e0 + y*(545.7340497931629e0 +
             (-50.91091728474331e0 - 18.30489878927802e0*y)*y))))) +
             x2*(268.5520265845071e0 + y*(-12019.028203559312e0 +
             y*(3734.858026725145e0 + y*(-2046.7671145057618e0 +
             y*(465.28655623826234e0 + (-0.6370820302376359e0 -
             10.650848542359153e0*y)*y)))) +
             x*(937.2099110620707e0 + y*(588.1802812170108e0+
             y*(248.39476522971285e0 + (-3.871557904936333e0-
             2.6268019854268356e0*y)*y)) +
             x*(-1687.914374187449e0 + x*(246.9598888781377e0 +
             x*(123.59576582457964e0 - 48.5891069025409e0*x)) +
             y*(936.3206544460336e0 +
             y*(-942.7827304544439e0 + y*(369.4389437509002e0 +
             (-33.83664947895248e0 - 9.987880382780322e0*y)*y))))));

        return (pot_enthalpy/gsw_cp0);
}

double
gsw_ct_from_t(double sa, double t, double p)
{
        double  pt0;

        pt0     = gsw_pt0_from_t(sa,t,p);
        return (gsw_ct_from_pt(sa,pt0));
}

double
gsw_specvol(double sa, double ct, double p)
{
        GSW_TEOS10_CONSTANTS;
        GSW_SPECVOL_COEFFICIENTS;
        double  xs, ys, z, value;

        xs      = sqrt(gsw_sfac*sa + offset);
        ys      = ct*0.025;
        z       = p*1e-4;

        value = v000
    + xs*(v010 + xs*(v020 + xs*(v030 + xs*(v040 + xs*(v050
    + v060*xs))))) + ys*(v100 + xs*(v110 + xs*(v120 + xs*(v130 + xs*(v140
    + v150*xs)))) + ys*(v200 + xs*(v210 + xs*(v220 + xs*(v230 + v240*xs)))
    + ys*(v300 + xs*(v310 + xs*(v320 + v330*xs)) + ys*(v400 + xs*(v410
    + v420*xs) + ys*(v500 + v510*xs + v600*ys))))) + z*(v001 + xs*(v011
    + xs*(v021 + xs*(v031 + xs*(v041 + v051*xs)))) + ys*(v101 + xs*(v111
    + xs*(v121 + xs*(v131 + v141*xs))) + ys*(v201 + xs*(v211 + xs*(v221
    + v231*xs)) + ys*(v301 + xs*(v311 + v321*xs) + ys*(v401 + v411*xs
    + v501*ys)))) + z*(v002 + xs*(v012 + xs*(v022 + xs*(v032 + v042*xs)))
    + ys*(v102 + xs*(v112 + xs*(v122 + v132*xs)) + ys*(v202 + xs*(v212
    + v222*xs) + ys*(v302 + v312*xs + v402*ys))) + z*(v003 + xs*(v013
    + v023*xs) + ys*(v103 + v113*xs + v203*ys) + z*(v004 + v014*xs + v104*ys
    + z*(v005 + v006*z)))));

        return (value);
}

double
gsw_rho(double sa, double ct, double p)
{
        return (1.0/gsw_specvol(sa,ct,p));
}

double
gsw_enthalpy_sso_0(double p)
{
        GSW_TEOS10_CONSTANTS;
        GSW_SPECVOL_COEFFICIENTS;
        double  dynamic_enthalpy_sso_0_p, z;

        z = p*1.0e-4;

        dynamic_enthalpy_sso_0_p =
                        z*( 9.726613854843870e-4 + z*(-2.252956605630465e-5
                        + z*( 2.376909655387404e-6 + z*(-1.664294869986011e-7
                        + z*(-5.988108894465758e-9 + z*(h006 + h007*z))))));
        return (dynamic_enthalpy_sso_0_p*db2pa*1.0e4);
}

double
gsw_z_from_p(double p, double lat, double geo_strf_dyn_height,
                double sea_surface_geopotential)
{
        GSW_TEOS10_CONSTANTS;
        double  x, sin2, b, c, a;

        x       = sin(lat*deg2rad);
        sin2    = x*x;
        b       = 9.780327*(1.0 + (5.2792e-3 + (2.32e-5*sin2))*sin2);
        a       = -0.5*gamma*b;
        c       = gsw_enthalpy_sso_0(p)
                  - (geo_strf_dyn_height + sea_surface_geopotential);

        return (-2.0*c/(b + sqrt(b*b - 4.0*a*c)));
}
