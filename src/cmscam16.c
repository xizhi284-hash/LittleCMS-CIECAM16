//---------------------------------------------------------------------------------
//
//  Little Color Management System
//  Copyright (c) 1998-2026 Marti Maria Saguer
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//---------------------------------------------------------------------------------
//

#include "lcms2_internal.h"

// CIECAM 16 appearance model, as defined in CIE 248:2022.
// Equation numbers in comments refer to that document.

// ---------- Implementation --------------------------------------------

typedef struct  {

    cmsFloat64Number XYZ[3];
    cmsFloat64Number RGB[3];
    cmsFloat64Number RGBc[3];
    cmsFloat64Number RGBa[3];
    cmsFloat64Number a, b, h, e, H, A, J, Q, s, t, C, M;

} CAM16COLOR;

typedef struct  {

    CAM16COLOR adoptedWhite;
    cmsFloat64Number LA, Yb;
    cmsFloat64Number F, c, Nc;
    cmsUInt32Number surround;
    cmsFloat64Number n, Nbb, Ncb, z, FL, D;

    cmsFloat64Number D_RGB[3];      // Colour-independent scaling factors (Eq. 5.4)
    cmsFloat64Number qU;            // Upper domain bound of the compression function (3.2)
    cmsFloat64Number f_qL;          // f(qL),  qL = 0.26   (Eq. 5.19)
    cmsFloat64Number f_qU;          // f(qU)               (Eq. 5.18)
    cmsFloat64Number df_qU;         // f'(qU)              (Eq. 5.21)

    cmsContext ContextID;

} cmsCIECAM16;


static
cmsFloat64Number compute_n(cmsCIECAM16* pMod)
{
    return (pMod -> Yb / pMod -> adoptedWhite.XYZ[1]);
}

static
cmsFloat64Number compute_z(cmsCIECAM16* pMod)
{
    return (1.48 + pow(pMod -> n, 0.5));
}

static
cmsFloat64Number computeNbb(cmsCIECAM16* pMod)
{
    return (0.725 * pow((1.0 / pMod -> n), 0.2));
}

static
cmsFloat64Number computeFL(cmsCIECAM16* pMod)
{
    cmsFloat64Number k, FL;

    k = 1.0 / ((5.0 * pMod -> LA) + 1.0);
    FL = 0.2 * pow(k, 4.0) * (5.0 * pMod -> LA) + 0.1 *
        (pow((1.0 - pow(k, 4.0)), 2.0)) *
        (pow((5.0 * pMod -> LA), (1.0 / 3.0)));

    return FL;
}

static
cmsFloat64Number computeD(cmsCIECAM16* pMod)
{
    cmsFloat64Number D, temp;

    temp = 1.0 - ((1.0 / 3.6) * exp((-pMod -> LA - 42) / 92.0));

    D = pMod -> F * temp;

    // CIE 248:2022 Annex A, step 2: D is constrained to the range [0, 1]
    if (D > 1.0) {
        D = 1.0;
    }
    if (D < 0.0) {
        D = 0.0;
    }

    return D;
}

static
CAM16COLOR XYZtoCAT16(CAM16COLOR clr)
{
    clr.RGB[0] = (clr.XYZ[0] *  0.401288) + (clr.XYZ[1] *  0.650173) + (clr.XYZ[2] * -0.051461);
    clr.RGB[1] = (clr.XYZ[0] * -0.250268) + (clr.XYZ[1] *  1.204414) + (clr.XYZ[2] *  0.045854);
    clr.RGB[2] = (clr.XYZ[0] * -0.002079) + (clr.XYZ[1] *  0.048952) + (clr.XYZ[2] *  0.953127);

    return clr;
}

// Eq. 5.4. Note the use of 100 instead of Yw (Gao et al. 2020)
static
void computeD_RGB(cmsCIECAM16* pMod)
{
    cmsUInt32Number i;

    for (i = 0; i < 3; i++) {
        pMod -> D_RGB[i] = (pMod -> D * 100.0 / pMod -> adoptedWhite.RGB[i]) + 1.0 - pMod -> D;
    }
}

static
CAM16COLOR ChromaticAdaptation(CAM16COLOR clr, cmsCIECAM16* pMod)
{
    cmsUInt32Number i;

    for (i = 0; i < 3; i++) {
        clr.RGBc[i] = pMod -> D_RGB[i] * clr.RGB[i];
    }

    return clr;
}

// The hyperbolic response compression function, f(q) of Eq. 3.4
static
cmsFloat64Number f_q(cmsCIECAM16* pMod, cmsFloat64Number q)
{
    cmsFloat64Number temp;

    temp = pow(pMod -> FL * q / 100.0, 0.42);

    return (400.0 * temp) / (temp + 27.13);
}

// The derivative of f(q), Eq. 3.5
static
cmsFloat64Number df_q(cmsCIECAM16* pMod, cmsFloat64Number q)
{
    cmsFloat64Number x;

    x = pMod -> FL * q / 100.0;

    return (1.68 * 27.13 * pMod -> FL * pow(x, -0.58)) /
        pow(27.13 + pow(x, 0.42), 2.0);
}

// Eq. 5.12: the plain hyperbolic function is used for the adopted white
static
CAM16COLOR WhiteNonlinearCompression(CAM16COLOR clr, cmsCIECAM16* pMod)
{
    cmsUInt32Number i;

    for (i = 0; i < 3; i++) {
        clr.RGBa[i] = f_q(pMod, clr.RGBc[i]) + 0.1;
    }

    clr.A = (((2.0 * clr.RGBa[0]) + clr.RGBa[1] +
        (clr.RGBa[2] / 20.0)) - 0.305) * pMod -> Nbb;

    return clr;
}

// Eqs. 5.16, 5.17: the modified hyperbolic function, with linear
// extensions below qL = 0.26 and above qU
static
CAM16COLOR NonlinearCompression(CAM16COLOR clr, cmsCIECAM16* pMod)
{
    cmsUInt32Number i;
    cmsFloat64Number q;

    for (i = 0; i < 3; i++) {

        q = clr.RGBc[i];

        if (q > pMod -> qU)
            clr.RGBa[i] = pMod -> f_qU + pMod -> df_qU * (q - pMod -> qU) + 0.1;
        else if (q >= 0.26)
            clr.RGBa[i] = f_q(pMod, q) + 0.1;
        else
            clr.RGBa[i] = pMod -> f_qL * q / 0.26 + 0.1;
    }

    clr.A = (((2.0 * clr.RGBa[0]) + clr.RGBa[1] +
        (clr.RGBa[2] / 20.0)) - 0.305) * pMod -> Nbb;

    return clr;
}

static
CAM16COLOR ComputeCorrelates(CAM16COLOR clr, cmsCIECAM16* pMod)
{
    cmsFloat64Number a, b, temp, e, t, r2d, d2r;

    a = clr.RGBa[0] - (12.0 * clr.RGBa[1] / 11.0) + (clr.RGBa[2] / 11.0);
    b = (clr.RGBa[0] + clr.RGBa[1] - (2.0 * clr.RGBa[2])) / 9.0;

    r2d = (180.0 / 3.141592654);
    clr.h = r2d * atan2(b, a);
    if (clr.h < 0.0) clr.h += 360.0;

    d2r = (3.141592654 / 180.0);
    e = ((12500.0 / 13.0) * pMod -> Nc * pMod -> Ncb) *
        (cos((clr.h * d2r + 2.0)) + 3.8);

    // Hue quadrature H from the unique hue data (Eq. 5.26, Table 3)
    if (clr.h < 20.14) {
        temp = ((clr.h + 122.47)/1.2) + ((20.14 - clr.h)/0.8);
        clr.H = 300 + (100*((clr.h + 122.47)/1.2)) / temp;
    }
    else if (clr.h < 90.0) {
        temp = ((clr.h - 20.14)/0.8) + ((90.00 - clr.h)/0.7);
        clr.H = (100*((clr.h - 20.14)/0.8)) / temp;
    }
    else if (clr.h < 164.25) {
        temp = ((clr.h - 90.00)/0.7) + ((164.25 - clr.h)/1.0);
        clr.H = 100 + ((100*((clr.h - 90.00)/0.7)) / temp);
    }
    else if (clr.h < 237.53) {
        temp = ((clr.h - 164.25)/1.0) + ((237.53 - clr.h)/1.2);
        clr.H = 200 + ((100*((clr.h - 164.25)/1.0)) / temp);
    }
    else {
        temp = ((clr.h - 237.53)/1.2) + ((360 - clr.h + 20.14)/0.8);
        clr.H = 300 + ((100*((clr.h - 237.53)/1.2)) / temp);
    }

    clr.J = 100.0 * pow((clr.A / pMod -> adoptedWhite.A),
        (pMod -> c * pMod -> z));

    clr.Q = (4.0 / pMod -> c) * pow((clr.J / 100.0), 0.5) *
        (pMod -> adoptedWhite.A + 4.0) * pow(pMod -> FL, 0.25);

    t = (e * pow(((a * a) + (b * b)), 0.5)) /
        (clr.RGBa[0] + clr.RGBa[1] +
        ((21.0 / 20.0) * clr.RGBa[2]));

    clr.C = pow(t, 0.9) * pow((clr.J / 100.0), 0.5) *
        pow((1.64 - pow(0.29, pMod -> n)), 0.73);

    clr.M = clr.C * pow(pMod -> FL, 0.25);

    if (clr.Q > 0.0)
        clr.s = 100.0 * pow((clr.M / clr.Q), 0.5);
    else
        clr.s = 0.0;    // J == 0: saturation is undefined, avoid divide by zero

    return clr;
}


static
CAM16COLOR InverseCorrelates(CAM16COLOR clr, cmsCIECAM16* pMod)
{

    cmsFloat64Number t, e, p1, p2, p3, p4, p5, hr, d2r;
    d2r = 3.141592654 / 180.0;

    t = pow( (clr.C / (pow((clr.J / 100.0), 0.5) *
        (pow((1.64 - pow(0.29, pMod -> n)), 0.73)))),
        (1.0 / 0.9) );
    e = ((12500.0 / 13.0) * pMod -> Nc * pMod -> Ncb) *
        (cos((clr.h * d2r + 2.0)) + 3.8);

    clr.A = pMod -> adoptedWhite.A * pow(
           (clr.J / 100.0),
           (1.0 / (pMod -> c * pMod -> z)));

    p2 = (clr.A / pMod -> Nbb) + 0.305;

    if ( t <= 0.0 ) {     // special case from spec notes, avoid divide by zero
        clr.a = clr.b = 0.0;
    }
    else {
        hr = clr.h * d2r;
        p1 = e / t;
        p3 = 21.0 / 20.0;

        if (fabs(sin(hr)) >= fabs(cos(hr))) {
            p4 = p1 / sin(hr);
            clr.b = (p2 * (2.0 + p3) * (460.0 / 1403.0)) /
                (p4 + (2.0 + p3) * (220.0 / 1403.0) *
                (cos(hr) / sin(hr)) - (27.0 / 1403.0) +
                p3 * (6300.0 / 1403.0));
            clr.a = clr.b * (cos(hr) / sin(hr));
        }
        else {
            p5 = p1 / cos(hr);
            clr.a = (p2 * (2.0 + p3) * (460.0 / 1403.0)) /
                (p5 + (2.0 + p3) * (220.0 / 1403.0) -
                ((27.0 / 1403.0) - p3 * (6300.0 / 1403.0)) *
                (sin(hr) / cos(hr)));
            clr.b = clr.a * (sin(hr) / cos(hr));
        }
    }

    clr.RGBa[0] = ((460.0 / 1403.0) * p2) +
              ((451.0 / 1403.0) * clr.a) +
              ((288.0 / 1403.0) * clr.b);
    clr.RGBa[1] = ((460.0 / 1403.0) * p2) -
              ((891.0 / 1403.0) * clr.a) -
              ((261.0 / 1403.0) * clr.b);
    clr.RGBa[2] = ((460.0 / 1403.0) * p2) -
              ((220.0 / 1403.0) * clr.a) -
              ((6300.0 / 1403.0) * clr.b);

    return clr;
}

// Eq. 6.15: inverse of the modified hyperbolic compression
static
CAM16COLOR InverseNonlinearity(CAM16COLOR clr, cmsCIECAM16* pMod)
{
    cmsUInt32Number i;
    cmsFloat64Number v;

    for (i = 0; i < 3; i++) {

        v = clr.RGBa[i] - 0.1;

        if (v > pMod -> f_qU)
            clr.RGBc[i] = pMod -> qU + (v - pMod -> f_qU) / pMod -> df_qU;
        else if (v >= pMod -> f_qL)
            clr.RGBc[i] = (100.0 / pMod -> FL) *
                pow((27.13 * v) / (400.0 - v), (1.0 / 0.42));
        else
            clr.RGBc[i] = 0.26 * v / pMod -> f_qL;
    }

    return clr;
}

static
CAM16COLOR InverseChromaticAdaptation(CAM16COLOR clr, cmsCIECAM16* pMod)
{
    cmsUInt32Number i;

    for (i = 0; i < 3; i++) {
        clr.RGB[i] = clr.RGBc[i] / pMod -> D_RGB[i];
    }

    return clr;
}


static
CAM16COLOR CAT16toXYZ(CAM16COLOR clr)
{
    clr.XYZ[0] = (clr.RGB[0] *  1.86206786) + (clr.RGB[1] * -1.01125463) + (clr.RGB[2] *  0.14918677);
    clr.XYZ[1] = (clr.RGB[0] *  0.38752654) + (clr.RGB[1] *  0.62144744) + (clr.RGB[2] * -0.00897398);
    clr.XYZ[2] = (clr.RGB[0] * -0.01584150) + (clr.RGB[1] * -0.03412294) + (clr.RGB[2] *  1.04996444);

    return clr;
}


cmsHANDLE  CMSEXPORT cmsCIECAM16Init(cmsContext ContextID, const cmsViewingConditions* pVC)
{
    cmsCIECAM16* lpMod;
    cmsUInt32Number i;

    _cmsAssert(pVC != NULL);

    // Yb == 0 is an error condition (CIE 248:2022, Clause 4):
    // the model does not apply to unrelated colours
    if (pVC -> Yb <= 0.0) {
        cmsSignalError(ContextID, cmsERROR_RANGE,
                       "cmsCIECAM16Init: Yb must be > 0 (unrelated colors not supported)");
        return NULL;
    }

    if((lpMod = (cmsCIECAM16*) _cmsMallocZero(ContextID, sizeof(cmsCIECAM16))) == NULL) {
        return NULL;
    }

    lpMod -> ContextID = ContextID;

    lpMod -> adoptedWhite.XYZ[0] = pVC -> whitePoint.X;
    lpMod -> adoptedWhite.XYZ[1] = pVC -> whitePoint.Y;
    lpMod -> adoptedWhite.XYZ[2] = pVC -> whitePoint.Z;

    lpMod -> LA       = pVC -> La;
    lpMod -> Yb       = pVC -> Yb;
    lpMod -> D        = pVC -> D_value;
    lpMod -> surround = pVC -> surround;

    switch (lpMod -> surround) {


    case CUTSHEET_SURROUND:
        // lcms extension, not defined by CIE 248:2022
        lpMod -> F = 0.8;
        lpMod -> c = 0.41;
        lpMod -> Nc = 0.8;
        break;

    case DARK_SURROUND:
        lpMod -> F  = 0.8;
        lpMod -> c  = 0.525;
        lpMod -> Nc = 0.8;
        break;

    case DIM_SURROUND:
        lpMod -> F  = 0.9;
        lpMod -> c  = 0.59;
        lpMod -> Nc = 0.9;
        break;

    default:
        // Average surround
        lpMod -> F  = 1.0;
        lpMod -> c  = 0.69;
        lpMod -> Nc = 1.0;
    }

    lpMod -> n   = compute_n(lpMod);
    lpMod -> z   = compute_z(lpMod);
    lpMod -> Nbb = computeNbb(lpMod);
    lpMod -> FL  = computeFL(lpMod);

    if (lpMod -> D == D_CALCULATE) {
        lpMod -> D   = computeD(lpMod);
    }

    lpMod -> Ncb = lpMod -> Nbb;

    lpMod -> adoptedWhite = XYZtoCAT16(lpMod -> adoptedWhite);
    computeD_RGB(lpMod);
    lpMod -> adoptedWhite = ChromaticAdaptation(lpMod -> adoptedWhite, lpMod);

    // 3.2: qU is 150 or the maximum of Rwc, Gwc, Bwc, whichever is larger
    lpMod -> qU = 150.0;
    for (i = 0; i < 3; i++) {
        if (lpMod -> adoptedWhite.RGBc[i] > lpMod -> qU)
            lpMod -> qU = lpMod -> adoptedWhite.RGBc[i];
    }

    lpMod -> f_qL  = f_q(lpMod, 0.26);
    lpMod -> f_qU  = f_q(lpMod, lpMod -> qU);
    lpMod -> df_qU = df_q(lpMod, lpMod -> qU);

    lpMod -> adoptedWhite = WhiteNonlinearCompression(lpMod -> adoptedWhite, lpMod);

    return (cmsHANDLE) lpMod;

}

void CMSEXPORT cmsCIECAM16Done(cmsHANDLE hModel)
{
    cmsCIECAM16* lpMod = (cmsCIECAM16*) hModel;

    if (lpMod) _cmsFree(lpMod -> ContextID, lpMod);
}


void CMSEXPORT cmsCIECAM16ForwardEx(cmsHANDLE hModel, const cmsCIEXYZ* pIn, cmsCIECAM16Appearance* pOut)
{
    CAM16COLOR clr;
    cmsCIECAM16* lpMod = (cmsCIECAM16*) hModel;

    _cmsAssert(lpMod != NULL);
    _cmsAssert(pIn != NULL);
    _cmsAssert(pOut != NULL);

    memset(&clr, 0, sizeof(clr));

    clr.XYZ[0] = pIn -> X;
    clr.XYZ[1] = pIn -> Y;
    clr.XYZ[2] = pIn -> Z;

    clr = XYZtoCAT16(clr);
    clr = ChromaticAdaptation(clr, lpMod);
    clr = NonlinearCompression(clr, lpMod);
    clr = ComputeCorrelates(clr, lpMod);

    pOut -> J = clr.J;
    pOut -> C = clr.C;
    pOut -> h = clr.h;
    pOut -> Q = clr.Q;
    pOut -> M = clr.M;
    pOut -> s = clr.s;
    pOut -> H = clr.H;
}


void CMSEXPORT cmsCIECAM16Forward(cmsHANDLE hModel, const cmsCIEXYZ* pIn, cmsJCh* pOut)
{
    cmsCIECAM16Appearance appearance;

    _cmsAssert(pOut != NULL);

    cmsCIECAM16ForwardEx(hModel, pIn, &appearance);

    pOut -> J = appearance.J;
    pOut -> C = appearance.C;
    pOut -> h = appearance.h;
}


void CMSEXPORT cmsCIECAM16ReverseEx(cmsHANDLE hModel, const cmsCIECAM16Appearance* pIn, cmsCIEXYZ* pOut)
{
    CAM16COLOR clr;
    cmsCIECAM16* lpMod = (cmsCIECAM16*) hModel;

    _cmsAssert(lpMod != NULL);
    _cmsAssert(pIn != NULL);
    _cmsAssert(pOut != NULL);

    memset(&clr, 0, sizeof(clr));

    // Only lightness, chroma and hue angle are used by the
    // reverse model; Q, M, s and H are ignored
    clr.J = pIn -> J;
    clr.C = pIn -> C;
    clr.h = pIn -> h;

    if (clr.J <= 0.0) {
        // Achromatic black, avoid divisions by zero down the pipeline
        pOut -> X = pOut -> Y = pOut -> Z = 0.0;
        return;
    }

    clr = InverseCorrelates(clr, lpMod);
    clr = InverseNonlinearity(clr, lpMod);
    clr = InverseChromaticAdaptation(clr, lpMod);
    clr = CAT16toXYZ(clr);

    pOut -> X = clr.XYZ[0];
    pOut -> Y = clr.XYZ[1];
    pOut -> Z = clr.XYZ[2];
}


void CMSEXPORT cmsCIECAM16Reverse(cmsHANDLE hModel, const cmsJCh* pIn, cmsCIEXYZ* pOut)
{
    cmsCIECAM16Appearance appearance;

    _cmsAssert(pIn != NULL);

    memset(&appearance, 0, sizeof(appearance));

    appearance.J = pIn -> J;
    appearance.C = pIn -> C;
    appearance.h = pIn -> h;

    cmsCIECAM16ReverseEx(hModel, &appearance, pOut);
}


// Two-step CAT16 chromatic adaptation transform (CIE 248:2022, Annex A).
// Computes the corresponding colour under a reference illuminant for a
// sample seen under a test illuminant.
cmsBool CMSEXPORT cmsCAT16(const cmsCIEXYZ* pWhiteSrc, cmsFloat64Number LaSrc,
                           const cmsCIEXYZ* pWhiteDst, cmsFloat64Number LaDst,
                           cmsUInt32Number surround,
                           const cmsCIEXYZ* pIn, cmsCIEXYZ* pOut)
{
    CAM16COLOR clr, whiteSrc, whiteDst;
    cmsFloat64Number F, Dsrc, Ddst;
    cmsFloat64Number Dsrc_RGB[3], Ddst_RGB[3];
    cmsUInt32Number i;

    _cmsAssert(pWhiteSrc != NULL);
    _cmsAssert(pWhiteDst != NULL);
    _cmsAssert(pIn != NULL);
    _cmsAssert(pOut != NULL);

    // Only the surround factor F is needed (Table 2)
    switch (surround) {

    case CUTSHEET_SURROUND:   // lcms extension, not defined by CIE 248:2022
    case DARK_SURROUND:
        F = 0.8;
        break;

    case DIM_SURROUND:
        F = 0.9;
        break;

    default:
        // Average surround
        F = 1.0;
    }

    memset(&clr, 0, sizeof(clr));
    memset(&whiteSrc, 0, sizeof(whiteSrc));
    memset(&whiteDst, 0, sizeof(whiteDst));

    clr.XYZ[0]      = pIn -> X;
    clr.XYZ[1]      = pIn -> Y;
    clr.XYZ[2]      = pIn -> Z;
    whiteSrc.XYZ[0] = pWhiteSrc -> X;
    whiteSrc.XYZ[1] = pWhiteSrc -> Y;
    whiteSrc.XYZ[2] = pWhiteSrc -> Z;
    whiteDst.XYZ[0] = pWhiteDst -> X;
    whiteDst.XYZ[1] = pWhiteDst -> Y;
    whiteDst.XYZ[2] = pWhiteDst -> Z;

    // Annex A, step 1
    clr      = XYZtoCAT16(clr);
    whiteSrc = XYZtoCAT16(whiteSrc);
    whiteDst = XYZtoCAT16(whiteDst);

    // Annex A, step 2 (Eq. 4.3), D constrained to the range [0, 1]
    Dsrc = F * (1.0 - ((1.0 / 3.6) * exp((-LaSrc - 42.0) / 92.0)));
    Ddst = F * (1.0 - ((1.0 / 3.6) * exp((-LaDst - 42.0) / 92.0)));

    if (Dsrc > 1.0) {
        Dsrc = 1.0;
    }
    else if (Dsrc < 0.0) {
        Dsrc = 0.0;
    }
    if (Ddst > 1.0) {
        Ddst = 1.0;
    }
    else if (Ddst < 0.0) {
        Ddst = 0.0;
    }

    // Annex A, steps 3 and 4 (Eqs. A.5 - A.14)
    for (i = 0; i < 3; i++) {
        Dsrc_RGB[i] = (Dsrc * 100.0 / whiteSrc.RGB[i]) + 1.0 - Dsrc;
        Ddst_RGB[i] = (Ddst * 100.0 / whiteDst.RGB[i]) + 1.0 - Ddst;
        clr.RGBc[i] = (Dsrc_RGB[i] / Ddst_RGB[i]) * clr.RGB[i];
    }

    // Annex A, step 5
    clr.RGB[0] = clr.RGBc[0];
    clr.RGB[1] = clr.RGBc[1];
    clr.RGB[2] = clr.RGBc[2];
    clr = CAT16toXYZ(clr);

    pOut -> X = clr.XYZ[0];
    pOut -> Y = clr.XYZ[1];
    pOut -> Z = clr.XYZ[2];

    return TRUE;
}
