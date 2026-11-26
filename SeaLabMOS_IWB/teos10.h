// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-GSW-additional-permission
// TEOS-10 subset: the GSW-C functions in teos10.c (GSW licence, LICENSES/LicenseRef-GSW.txt). See teos10.c.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
double gsw_sp_from_c(double c, double t, double p);       // c mS/cm, t deg C (ITS-90), p sea pressure dbar -> PSS-78
double gsw_sr_from_sp(double sp);                          // Reference Salinity, g/kg (used as Absolute Salinity: SAAR = 0)
double gsw_ct_from_t(double sa, double t, double p);       // Conservative Temperature
double gsw_rho(double sa, double ct, double p);            // in-situ density, kg/m^3 (75-term)
double gsw_z_from_p(double p, double lat, double geo_strf_dyn_height, double sea_surface_geopotential);   // height, m (negative below the sea surface)
#ifdef __cplusplus
}
#endif
