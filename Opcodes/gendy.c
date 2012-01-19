/*
    gendy.c:

    Implementation of the dynamic stochastic synthesis generator
    conceived by Iannis Xenakis.

    Based on Nick Collins's Gendy1 ugen (SuperCollider).

    (c) Tito Latini, 2012

    This file is part of Csound.

    The Csound Library is free software; you can redistribute it
    and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Csound is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with Csound; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

#include <csound/csoundCore.h>

typedef struct {
        OPDS    h;
        MYFLT   *out, *kamp, *ampdist, *durdist, *adpar, *ddpar;
        MYFLT   *minfreq, *maxfreq, *ampscl, *durscl, *initcps, *knum;
        MYFLT   phase, amp, nextamp, dur, speed;
        int32   index, rand;
        AUXCH   memamp, memdur;
} GENDY;

typedef struct {
        OPDS    h;
        MYFLT   *out, *kamp, *ampdist, *durdist, *adpar, *ddpar;
        MYFLT   *minfreq, *maxfreq, *ampscl, *durscl;
        MYFLT   *kcurveup, *kcurvedown, *initcps, *knum;
        MYFLT   phase, amp, nextamp, dur, speed;
        int32   index, rand;
        AUXCH   memamp, memdur;
} GENDYX;

#define BIPOLAR      0x7FFFFFFF    /* Constant to make bipolar */
#define dv2_31       (FL(4.656612873077392578125e-10))
#define GENDYMAXCPS  8192          /* Max number of control points */

MYFLT gendy_distribution(CSOUND *csound, int which, MYFLT a, int32 rnd)
{
    MYFLT   c, r;
    if (a > FL(1.0))
      a = FL(1.0);
    else if (a < FL(0.0001))
      a = FL(0.0001);
    switch (which) {
    case 0: // linear
      break;
    case 1: // cauchy
      c = ATAN(FL(10.0)*a);
      r = (MYFLT)((int32)((unsigned)rnd<<1)-BIPOLAR) * dv2_31;
      r = (FL(1.0)/a) * TAN(c*r) * FL(0.1);
      return r;
    case 2: // logist
      c = FL(0.5)+(FL(0.499)*a);
      c = LOG((FL(1.0)-c)/c);
      r = (((MYFLT)rnd*dv2_31 - FL(0.5)) * FL(0.998)*a) + FL(0.5);
      r = LOG((FL(1.0)-r)/r)/c;
      return r;
    case 3: // hyperbcos
      c = TAN(FL(1.5692255)*a);
      r = TAN(FL(1.5692255)*a*(MYFLT)rnd*dv2_31)/c;
      r = (LOG(r*FL(0.999)+FL(0.001)) * FL(-0.1447648))*FL(2.0) - FL(1.0);
      return r;
    case 4: // arcsine
      c = SIN(FL(1.5707963)*a);
      r = SIN(PI_F * ((MYFLT)rnd*dv2_31 - FL(0.5))*a)/c;
      return r;
    case 5: // expon
      c = LOG(FL(1.0)-(FL(0.999)*a));
      r = (MYFLT)rnd*dv2_31*FL(0.999)*a;
      r = (LOG(FL(1.0)-r)/c)*FL(2.0) - FL(1.0);
      return r;
    case 6: // external sig
      return a;
    default:
      break;
    }
    r = (MYFLT)((int32)((unsigned)rnd<<1)-BIPOLAR) * dv2_31;
    return r;
}

int gendy_init(CSOUND *csound, GENDY *p)
{
    int32   initcps, i;
    MYFLT   *memamp, *memdur;
    p->amp     = FL(0.0);
    p->nextamp = FL(0.0);
    p->phase   = FL(1.0);
    p->speed   = FL(100.0);
    p->index   = 0;
    if (*p->initcps < FL(1.0))
      *p->initcps = FL(12.0);
    else if (*p->initcps > GENDYMAXCPS)
      *p->initcps = GENDYMAXCPS;
    initcps = (int32) *p->initcps;
    csound->AuxAlloc(csound, initcps*sizeof(MYFLT), &p->memamp);
    csound->AuxAlloc(csound, initcps*sizeof(MYFLT), &p->memdur);
    memamp  = p->memamp.auxp;
    memdur  = p->memdur.auxp;
    p->rand = csoundRand31(&csound->randSeed1);
    for (i=0; i < initcps; i++) {
      p->rand = csoundRand31(&p->rand);
      memamp[i] = (MYFLT)((int32)((unsigned)p->rand<<1)-BIPOLAR)*dv2_31;
      p->rand = csoundRand31(&p->rand);
      memdur[i] = (MYFLT)p->rand * dv2_31;
    }
    return OK;
}

int gendy_process_arate(CSOUND *csound, GENDY *p)
{
    int     knum, n, nn = csound->ksmps;
    MYFLT   *out, *memamp, *memdur, minfreq, maxfreq, dist;
    int32   initcps = (int32)*p->initcps;
    out  = p->out;
    knum = (int)*p->knum;
    memamp  = p->memamp.auxp;
    memdur  = p->memdur.auxp;
    minfreq = *p->minfreq;
    maxfreq = *p->maxfreq;
    for (n=0; n<nn; n++) {
      if (p->phase >= FL(1.0)) {
        int index = p->index;
        p->phase -= FL(1.0);
        if (knum > initcps || knum < 1)
          knum = initcps;
        p->index = index = (index+1) % knum;
        p->amp = p->nextamp;
        p->rand = csoundRand31(&p->rand);
        dist = gendy_distribution(csound, *p->ampdist, *p->adpar, p->rand);
        p->nextamp = memamp[index] + *p->ampscl * dist;
        /* amplitude variations within the boundaries of a mirror */
        if (p->nextamp < FL(-1.0) || p->nextamp > FL(1.0)) {
          if (p->nextamp < FL(0.0))
            p->nextamp += FL(4.0);
          p->nextamp = FMOD(p->nextamp, FL(4.0));
          if (p->nextamp > FL(1.0)) {
            p->nextamp =
              (p->nextamp < FL(3.0) ? FL(2.0) - p->nextamp : p->nextamp - FL(4.0));
          }
        }
        memamp[index] = p->nextamp;
        p->rand = csoundRand31(&p->rand);
        dist = gendy_distribution(csound, *p->durdist, *p->ddpar, p->rand);
        p->dur = memdur[index] + *p->durscl * dist;
        /* time variations within the boundaries of a mirror */
        if (p->dur > FL(1.0))
          p->dur = FL(2.0) - FMOD(p->dur, FL(2.0));
        else if (p->dur < FL(0.0))
          p->dur = FL(2.0) - FMOD(p->dur + FL(2.0), FL(2.0));
        memdur[index] = p->dur;
        p->speed =
          (minfreq + (maxfreq - minfreq) * p->dur) * csound->onedsr * knum;
      }
      out[n] = *p->kamp * ((FL(1.0) - p->phase) * p->amp + p->phase * p->nextamp);
      p->phase += p->speed;
    }
    return OK;
}

int gendyx_init(CSOUND *csound, GENDYX *p)
{
    int32   initcps, i;
    MYFLT   *memamp, *memdur;
    p->amp     = FL(0.0);
    p->nextamp = FL(0.0);
    p->phase   = FL(1.0);
    p->speed   = FL(100.0);
    p->index   = 0;
    if (*p->initcps < FL(1.0))
      *p->initcps = FL(12.0);
    else if (*p->initcps > GENDYMAXCPS)
      *p->initcps = GENDYMAXCPS;
    initcps = (int32) *p->initcps;
    csound->AuxAlloc(csound, initcps*sizeof(MYFLT), &p->memamp);
    csound->AuxAlloc(csound, initcps*sizeof(MYFLT), &p->memdur);
    memamp  = p->memamp.auxp;
    memdur  = p->memdur.auxp;
    p->rand = csoundRand31(&csound->randSeed1);
    for (i=0; i < initcps; i++) {
      p->rand   = (int32)csoundRand31(&p->rand);
      memamp[i] = (MYFLT)((int32)((unsigned)p->rand<<1)-BIPOLAR)*dv2_31;
      p->rand   = csoundRand31(&p->rand);
      memdur[i] = (MYFLT)p->rand * dv2_31;
    }
    return OK;
}

int gendyx_process_arate(CSOUND *csound, GENDYX *p)
{
    int     knum, n, nn = csound->ksmps;
    MYFLT   *out, *memamp, *memdur, minfreq, maxfreq, dist, curve;
    int32   initcps = (int32)*p->initcps;
    out  = p->out;
    knum = (int)*p->knum;
    memamp  = p->memamp.auxp;
    memdur  = p->memdur.auxp;
    minfreq = *p->minfreq;
    maxfreq = *p->maxfreq;
    for (n=0; n<nn; n++) {
      if (p->phase >= FL(1.0)) {
        int index = p->index;
        p->phase -= FL(1.0);
        if (knum > initcps || knum < 1)
          knum = initcps;
        p->index = index = (index+1) % knum;
        p->amp = p->nextamp;
        p->rand = csoundRand31(&p->rand);
        dist = gendy_distribution(csound, *p->ampdist, *p->adpar, p->rand);
        p->nextamp = memamp[index] + *p->ampscl * dist;
        if (p->nextamp < FL(-1.0) || p->nextamp > FL(1.0)) {
          if (p->nextamp < FL(0.0))
            p->nextamp += FL(4.0);
          p->nextamp = FMOD(p->nextamp, FL(4.0));
          if (p->nextamp > FL(1.0)) {
            p->nextamp =
              (p->nextamp < FL(3.0) ? FL(2.0) - p->nextamp : p->nextamp - FL(4.0));
          }
        }
        memamp[index] = p->nextamp;
        p->rand = csoundRand31(&p->rand);
        dist = gendy_distribution(csound, *p->durdist, *p->ddpar, p->rand);
        p->dur = memdur[index] + *p->durscl * dist;
        if (p->dur > FL(1.0))
          p->dur = FL(2.0) - FMOD(p->dur, FL(2.0));
        else if (p->dur < FL(0.0))
          p->dur = FL(2.0) - FMOD(p->dur + FL(2.0), FL(2.0));
        memdur[index] = p->dur;
        p->speed =
          (minfreq + (maxfreq - minfreq) * p->dur) * csound->onedsr * knum;
      }
      if (*p->kcurveup < FL(0.0))
        *p->kcurveup = FL(0.0);
      if (*p->kcurvedown < FL(0.0))
        *p->kcurvedown = FL(0.0);
      curve = ((p->nextamp - p->amp) > FL(0.0) ? *p->kcurveup : *p->kcurvedown);
      out[n] = *p->kamp * (p->amp + POWER(p->phase, curve) * (p->nextamp - p->amp));
      p->phase += p->speed;
    }
    return OK;
}

static OENTRY gendy_localops[] = {
  { "gendy", sizeof(GENDY), 5, "a", "kkkkkkkkkoO",
    (SUBR)gendy_init, NULL, (SUBR)gendy_process_arate },
  { "gendyx", sizeof(GENDYX), 5, "a", "kkkkkkkkkkkoO",
    (SUBR)gendyx_init, NULL, (SUBR)gendyx_process_arate }
};

LINKAGE1(gendy_localops)
