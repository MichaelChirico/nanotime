#include <sstream>
#include <regex>
#include <Rcpp.h>
#include "period.hpp"
#include "duration.hpp"
#include "date.h"
#include "pseudovector.hpp"
#include "utilities.hpp"


// for debug reasons...
// the following code from: https://stackoverflow.com/a/16692519
template<typename Clock, typename Duration>
std::ostream &operator<<(std::ostream &stream,
                         const std::chrono::time_point<Clock, Duration> &time_point) {
  const time_t time = Clock::to_time_t(time_point);
#if __GNUC__ > 4 || \
   ((__GNUC__ == 4) && __GNUC_MINOR__ > 8 && __GNUC_REVISION__ > 1)
    // Maybe the put_time will be implemented later?
    struct tm tm;
  localtime_r(&time, &tm);
  return stream << std::put_time(&tm, "%c"); // Print standard date&time
#else
  char buffer[26];
  ctime_r(&time, buffer);
  buffer[24] = '\0';  // Removes the newline that is added
  return stream << buffer;
#endif
}


extern "C" int getOffset(long long s, const char* tzstr);

static inline Global::duration getOffsetCnv(const Global::dtime& dt, const std::string& z) {
  typedef int GET_OFFSET_FUN(long long, const char*); 
  GET_OFFSET_FUN *getOffset = (GET_OFFSET_FUN *) R_GetCCallable("RcppCCTZ", "_RcppCCTZ_getOffset" );

  auto offset = getOffset(std::chrono::duration_cast<std::chrono::seconds>(dt.time_since_epoch()).count(), z.c_str());
  return Global::duration(offset).count() * std::chrono::seconds(1);
}

period::period() : months(0), days(0), dur(std::chrono::seconds(0)) { }

period::period(int32_t months_p, int32_t days_p, Global::duration dur_p) : 
  months(months_p), days(days_p), dur(dur_p) { }


period::period(const std::string& str) {
  const char* s = str.c_str();
  const char* e = s + str.size();

  months = 0;
  days   = 0;
  dur    = std::chrono::seconds(0);

  int n;
  if (s < e && (*s == '/' || (s+2 < e && s[2] == ':'))) goto getduration;
  if (!Global::readNumber(s, e, n, true) || s == e) throw std::range_error("cannot parse nanoperiod");
  if (*s == 'y') {
    months += 12*n;
    ++s;
    if (s < e) {
      if (*s == '/') goto getduration;
      if (!Global::readNumber(s, e, n, true) || s == e) throw std::range_error("cannot parse nanoperiod");
    }
    else {
      return;
    }
  }      
  if (*s == 'm') {
    months += n;
    ++s;
    if (s < e) {
      if (*s == '/') goto getduration;
      if (!Global::readNumber(s, e, n, true) || s == e) throw std::range_error("cannot parse nanoperiod");      
    }
    else {
      return;
    }
  }      
  if (*s == 'w') {
    days += 7*n;
    ++s;
    if (s < e) {
      if (*s == '/') goto getduration;
      if (!Global::readNumber(s, e, n, true) || s == e) throw std::range_error("cannot parse nanoperiod");
    }
    else {
      return;
    }
  }
  if (*s == 'd') {
    days += n;
    ++s;
    if (s < e) { 
      if (*s == '/') goto getduration;
      if (!Global::readNumber(s, e, n, true) || s == e) throw std::range_error("cannot parse nanoperiod");
    }
    else {
      return;
    }
  }

  // we've succeeded a Global::readNumber, so this means we've
  // actually read into the duration; so backtrack and use the already
  // existing function to parse a duration:
getduration:                    // # nocov
  try {
    dur = from_string(++s);
  }
  catch (...) {
    throw std::range_error("cannot parse nanoperiod");
  }
}


std::string to_string(const period& p) {
  std::stringstream ss;
  ss << p.getMonths() << "m" << p.getDays() << "d/" << to_string(p.getDuration());
  return ss.str();
}


period plus (const period&    p, Global::duration d) {
  return period(p.getMonths(), p.getDays(), p.getDuration() + d);
}
period minus(const period&    p, Global::duration d){
  return period(p.getMonths(), p.getDays(), p.getDuration() - d);
}
period minus(Global::duration d, const period& p) {
  return period(-p.getMonths(), -p.getDays(), -p.getDuration() + d);
}

Global::dtime plus (const Global::dtime& dt, const period& p, const std::string& z) {
  auto res = dt;
  auto offset = getOffsetCnv(res, z);
  if (p.getMonths()) {
    auto dt_floor = date::floor<date::days>(dt + offset);
    auto timeofday_offset = (dt + offset) - dt_floor;
    auto dt_ymd = date::year_month_day{dt_floor};
    dt_ymd += date::months(p.getMonths());
    res = date::sys_days(dt_ymd) - offset + timeofday_offset;
  }
  offset = getOffsetCnv(dt, z);
  res += p.getDays()*std::chrono::hours(24);
  res += p.getDuration();
  auto newoffset = getOffsetCnv(res, z);
  if (newoffset != offset) {
    res += offset - newoffset; // adjust for DST or any other event that changed the TZ
  }
  return res;
}

Global::dtime minus(const Global::dtime& dt, const period& p, const std::string& z) {
  return plus(dt, -p, z);
}

interval plus(const interval& i, const period& p, const std::string& z) {
  return interval(plus(Global::dtime{Global::duration{i.s}}, p, z),
                  plus(Global::dtime{Global::duration{i.e}}, p, z), i.sopen, i.eopen);
}
  
interval minus(const interval& i, const period& p, const std::string& z) {
  return plus(i, -p, z);
}


period operator+(const period& p1, const period& p2) {
  return period(p1.getMonths()+p2.getMonths(), 
                p1.getDays()+p2.getDays(), 
                p1.getDuration()+p2.getDuration());
}

period operator-(const period& p) {
  return period(-p.getMonths(), -p.getDays(), -p.getDuration());
}

period operator-(const period& p1, const period& p2) {
  return period(p1.getMonths()-p2.getMonths(), 
                p1.getDays()-p2.getDays(),
                p1.getDuration()-p2.getDuration());
}

template <typename T>
period operator*(const period& p, T d) {
  return period(p.getMonths()*d, 
                p.getDays()*d,
                Global::duration(static_cast<int64_t>(d*p.getDuration().count())*
                                 Global::duration(1)));
}

template <typename T>
period operator/(const period& p, T d) {
  if (d == 0) {
    throw std::logic_error("divide by zero");
  }
  return period(p.getMonths()/d, 
                p.getDays()/d,
                Global::duration(static_cast<int64_t>(p.getDuration().count()/d)*
                                 Global::duration(1)));
}

// bool operator>(const period& p1, const period& p2) {
//   // this is actually a difficult proposition, so for this calculation
//   // we take into account the average lengths. This means that in
//   // certain specific applications p1 might be <= to p2. But at any
//   // rate this should work for all practical purposes:
//   const auto YEAR = 365.25 * 24h;
//   const auto MONTH = YEAR/12;
//   return p1.getMonths()*MONTH + p1.getDays()*24h < p2.getMonths()*MONTH + p2.getDays()*24h;
// }

bool operator==(const period& p1, const period& p2) {
  return 
    p1.getMonths() == p2.getMonths() && 
    p1.getDays() == p2.getDays() &&
    p1.getDuration() == p2.getDuration();
}

bool operator!=(const period& p1, const period& p2) {
  return !(p1 == p2);
}


struct double2 {
  double d1;
  double d2;
};

union period_union {
  struct period_alias {
    int32_t i1;
    int32_t i2;
    int64_t i3;  
  } prd;
  double2 dbl2;
};
  

// const int PRDSZ   = sizeof(period_union)/sizeof(double);
// const int INT64SZ = 1;
// const int NANOSZ  = 1;
// const int REALSZ  = 1;

// see Rcpp/inst/include/Rcpp/vector/instantiation.h where NumericVector and al. are defined
typedef ConstPseudoVector<REALSXP, double>   ConstPseudoVectorInt64;
typedef ConstPseudoVector<REALSXP, double>   ConstPseudoVectorNano;
typedef ConstPseudoVector<CPLXSXP, Rcomplex> ConstPseudoVectorPrd;
typedef ConstPseudoVector<REALSXP, double>   ConstPseudoVectorDbl;
typedef ConstPseudoVector<CPLXSXP, Rcomplex> ConstPseudoVectorIval;
typedef ConstPseudoVector<STRSXP,  const Rcpp::CharacterVector::const_Proxy> ConstPseudoVectorChar;

typedef PseudoVector<REALSXP, double>   PseudoVectorInt64;
typedef PseudoVector<REALSXP, double>   PseudoVectorNano;
typedef PseudoVector<CPLXSXP, Rcomplex> PseudoVectorPrd;

// [[Rcpp::export]]
Rcpp::ComplexVector period_from_string_impl(Rcpp::CharacterVector str) {
  Rcpp::ComplexVector res(str.size());
  for (R_xlen_t i=0; i<str.size(); ++i) {
    period prd(Rcpp::as<std::string>(str[i]));
    period_union pu = { prd.getMonths(), prd.getDays(), prd.getDuration().count() };
    res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
  }
  if (str.hasAttribute("names")) {
    res.names() = str.names();
  }
  return res;
}


// [[Rcpp::export]]
Rcpp::CharacterVector period_to_string_impl(Rcpp::ComplexVector prd) {
  Rcpp::CharacterVector res(prd.size());
  for (R_xlen_t i=0; i<prd.size(); ++i) {
    period pu;
    memcpy(&pu, reinterpret_cast<const char*>(&prd[i]), sizeof(period));
    if (pu.isNA()) {
      res[i] = NA_STRING;
    }
    else {
      res[i] = to_string(*reinterpret_cast<period*>(&pu));
    }
  }
  if (prd.hasAttribute("names")) {
    Rcpp::CharacterVector prdnm(prd.names());
    Rcpp::CharacterVector nm(prdnm.size());
    for (R_xlen_t i=0; i<nm.size(); ++i) {
      nm[i] = prdnm[i];
    }
    if (prd.hasAttribute("names")) {
      res.names() = prd.names();
    }
    res.names() = nm;
  }
  return res;
}

typedef std::numeric_limits< double > dbl;

// [[Rcpp::export]]
Rcpp::ComplexVector period_from_integer64_impl(Rcpp::NumericVector i64) {
  Rcpp::ComplexVector res(i64.size());
  for (R_xlen_t i=0; i<i64.size(); ++i) {
    auto elt = *reinterpret_cast<std::int64_t*>(&i64[i]);
    if (elt == Global::NA_INTEGER64) {
      period_union pu = { NA_INTEGER, NA_INTEGER, Global::NA_INTEGER64 };
      res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
    }
    else {
      period_union pu = { 0, 0, elt };
      res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
    }
  }
  if (i64.hasAttribute("names")) {
    res.names() = i64.names();
  }
  return res;
}


// [[Rcpp::export]]
Rcpp::ComplexVector period_from_integer_impl(Rcpp::IntegerVector iint) {
  Rcpp::ComplexVector res(iint.size());
  for (R_xlen_t i=0; i<iint.size(); ++i) {
    if (iint[i] == NA_INTEGER) {
      period_union pu = { NA_INTEGER, NA_INTEGER, Global::NA_INTEGER64 };
      res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
    }
    else {
      period_union pu = { 0, 0, static_cast<std::int64_t>(iint[i]) };
      res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
    }
  }
  if (iint.hasAttribute("names")) {
    res.names() = iint.names();
  }
  return res;
}


// [[Rcpp::export]]
Rcpp::ComplexVector period_from_double_impl(Rcpp::NumericVector dbl) {
  Rcpp::ComplexVector res(dbl.size());
  for (R_xlen_t i=0; i<dbl.size(); ++i) {
    if (ISNA(dbl[i])) {
      period_union pu = { NA_INTEGER, NA_INTEGER, Global::NA_INTEGER64 };
      res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
    }
    else {
      period_union pu = { 0, 0, static_cast<std::int64_t>(dbl[i]) };
      res[i] = Rcomplex{pu.dbl2.d1, pu.dbl2.d2 };
    }
  }
  if (dbl.hasAttribute("names")) {
    res.names() = dbl.names();
  }
  return res;
}


// [[Rcpp::export]]
Rcpp::ComplexVector plus_period_period_impl(const Rcpp::ComplexVector e1_nv,
                                            const Rcpp::ComplexVector e2_nv) {
  checkVectorsLengths(e1_nv, e2_nv);
  const ConstPseudoVectorPrd e1_n(e1_nv);
  const ConstPseudoVectorPrd e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_nv.size(), e2_nv.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    period pu2; memcpy(&pu2, reinterpret_cast<const char*>(&e2_n[i]), sizeof(period));
    auto prd = pu1 + pu2;
    memcpy(&res[i], &prd, sizeof(prd));
  }
  copyNames(e1_nv, e2_nv, res);
  return assignS4("nanoperiod", res);
}


// unary '-'
// [[Rcpp::export]]
Rcpp::ComplexVector minus_period_impl(const Rcpp::ComplexVector e1_cv) {
  const ConstPseudoVectorPrd e1_n(e1_cv);
  Rcpp::ComplexVector res(e1_cv.size());
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    auto prd = -pu1;
    memcpy(&res[i], reinterpret_cast<const char*>(&prd), sizeof(prd));
  }
  copyNames(e1_cv, e1_cv, res);
  return assignS4("nanoperiod", res);
}


// [[Rcpp::export]]
Rcpp::ComplexVector minus_period_period_impl(const Rcpp::ComplexVector e1_cv,
                                             const Rcpp::ComplexVector e2_cv) {
  checkVectorsLengths(e1_cv, e2_cv);
  const ConstPseudoVectorPrd e1_n(e1_cv);
  const ConstPseudoVectorPrd e2_n(e2_cv);
  Rcpp::ComplexVector res(std::max(e1_cv.size(), e2_cv.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    period pu2; memcpy(&pu2, reinterpret_cast<const char*>(&e2_n[i]), sizeof(period));
    auto prd = pu1 - pu2;
    memcpy(&res[i], reinterpret_cast<const char*>(&prd), sizeof(prd));
  }
  copyNames(e1_cv, e2_cv, res);
  return assignS4("nanoperiod", res);
}


template <typename OP>
Rcpp::LogicalVector compare_period_period(const Rcpp::ComplexVector e1_cv,
                                          const Rcpp::ComplexVector e2_cv,
                                          const OP& op) {
  checkVectorsLengths(e1_cv, e2_cv);
  const ConstPseudoVectorPrd e1_n(e1_cv);
  const ConstPseudoVectorPrd e2_n(e2_cv);
  Rcpp::LogicalVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    period pu2; memcpy(&pu2, reinterpret_cast<const char*>(&e2_n[i]), sizeof(period));
    res[i] = op(pu1, pu2);
  }
  copyNames(e1_cv, e2_cv, res);
  return res;
}

// [[Rcpp::export]]
Rcpp::LogicalVector eq_period_period_impl(const Rcpp::ComplexVector e1_p,
                                          const Rcpp::ComplexVector e2_p) {
  return compare_period_period(e1_p, e2_p, std::equal_to<period>());
}

// [[Rcpp::export]]
Rcpp::LogicalVector ne_period_period_impl(const Rcpp::ComplexVector e1_p,
                                          const Rcpp::ComplexVector e2_p) {
  return compare_period_period(e1_p, e2_p, std::not_equal_to<period>());
}

// [[Rcpp::export]]
Rcpp::ComplexVector plus_period_integer64_impl(const Rcpp::ComplexVector e1_cv,
                                               const Rcpp::NumericVector e2_nv) {
  checkVectorsLengths(e1_cv, e2_nv);
  const ConstPseudoVectorPrd   e1_n(e1_cv);
  const ConstPseudoVectorInt64 e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    Global::duration dur; memcpy(&dur, reinterpret_cast<const char*>(&e2_n[i]), sizeof(dur));
    pu1 = plus(pu1, dur);
    memcpy(&res[i], &pu1, sizeof(pu1));
  }
  copyNames(e1_cv, e2_nv, res);
  return assignS4("nanoperiod", res);
}


// [[Rcpp::export]]
Rcpp::ComplexVector  minus_period_integer64_impl(const Rcpp::ComplexVector e1_cv,
                                                 const Rcpp::NumericVector e2_nv) {
  checkVectorsLengths(e1_cv, e2_nv);
  const ConstPseudoVectorPrd   e1_n(e1_cv);
  const ConstPseudoVectorInt64 e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    Global::duration dur; memcpy(&dur, reinterpret_cast<const char*>(&e2_n[i]), sizeof(dur));
    pu1 = minus(pu1, dur);
    memcpy(&res[i], &pu1, sizeof(pu1));
  }
  copyNames(e1_cv, e2_nv, res);
  return assignS4("nanoperiod", res);
}

// [[Rcpp::export]]
Rcpp::ComplexVector multiplies_period_integer64_impl(const Rcpp::ComplexVector e1_cv,
                                                     const Rcpp::NumericVector e2_nv) {
  checkVectorsLengths(e1_cv, e2_nv);
  const ConstPseudoVectorPrd   e1_n(e1_cv);
  const ConstPseudoVectorInt64 e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    uint64_t m; memcpy(&m, reinterpret_cast<const char*>(&e2_n[i]), sizeof(m));
    pu1 = pu1 * m;
    memcpy(&res[i], &pu1, sizeof(pu1));
  }
  copyNames(e1_cv, e2_nv, res);
  return assignS4("nanoperiod", res);
}

// [[Rcpp::export]]
Rcpp::ComplexVector divides_period_integer64_impl(const Rcpp::ComplexVector e1_cv,
                                                  const Rcpp::NumericVector e2_nv) {
  checkVectorsLengths(e1_cv, e2_nv);
  const ConstPseudoVectorPrd   e1_n(e1_cv);
  const ConstPseudoVectorInt64 e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    uint64_t m; memcpy(&m,   reinterpret_cast<const char*>(&e2_n[i]), sizeof(m));
    pu1 = pu1 / m;
    memcpy(&res[i], &pu1, sizeof(pu1));
  }
  copyNames(e1_cv, e2_nv, res);
  return assignS4("nanoperiod", res);
}

// [[Rcpp::export]]
Rcpp::ComplexVector multiplies_period_double_impl(const Rcpp::ComplexVector e1_cv,
                                                  const Rcpp::NumericVector e2_nv) {
  checkVectorsLengths(e1_cv, e2_nv);
  const ConstPseudoVectorPrd e1_n(e1_cv);
  const ConstPseudoVectorDbl e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    double m;   memcpy(&m,   reinterpret_cast<const char*>(&e2_n[i]), sizeof(m));
    pu1 = pu1 * m;
    memcpy(&res[i], &pu1, sizeof(pu1));
  }
  copyNames(e1_cv, e2_nv, res);
  return assignS4("nanoperiod", res);
}

// [[Rcpp::export]]
Rcpp::ComplexVector divides_period_double_impl(const Rcpp::ComplexVector e1_cv,
                                               const Rcpp::NumericVector e2_nv) {
  checkVectorsLengths(e1_cv, e2_nv);
  const ConstPseudoVectorPrd e1_n(e1_cv);
  const ConstPseudoVectorDbl e2_n(e2_nv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu1; memcpy(&pu1, reinterpret_cast<const char*>(&e1_n[i]), sizeof(period));
    double m;   memcpy(&m,   reinterpret_cast<const char*>(&e2_n[i]), sizeof(m));
    pu1 = pu1 / m;
    memcpy(&res[i], &pu1, sizeof(pu1));
  }
  copyNames(e1_cv, e2_nv, res);
  return assignS4("nanoperiod", res);
}

// [[Rcpp::export]]
Rcpp::ComplexVector minus_integer64_period_impl(const Rcpp::NumericVector e1_nv,
                                                const Rcpp::ComplexVector e2_cv) {
  checkVectorsLengths(e1_nv, e2_cv);
  const ConstPseudoVectorInt64 e1_n(e1_nv);
  const ConstPseudoVectorPrd   e2_n(e2_cv);
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    period pu2;           memcpy(&pu2, reinterpret_cast<const char*>(&e2_n[i]), sizeof(pu2));
    Global::duration dur; memcpy(&dur, reinterpret_cast<const char*>(&e1_n[i]), sizeof(dur));
    pu2 = minus(dur, pu2);
    memcpy(&res[i], &pu2, sizeof(pu2));
  }
  copyNames(e1_nv, e2_cv, res);
  return assignS4("nanoperiod", res);
}

// [[Rcpp::export]]
Rcpp::NumericVector plus_nanotime_period_impl(const Rcpp::NumericVector   e1_nv,
                                              const Rcpp::ComplexVector   e2_cv,
                                              const Rcpp::CharacterVector tz_v) {
  checkVectorsLengths(e1_nv, e2_cv);
  checkVectorsLengths(e1_nv, tz_v);
  checkVectorsLengths(e2_cv, tz_v);
  const ConstPseudoVectorNano e1_n(e1_nv);
  const ConstPseudoVectorPrd  e2_n(e2_cv);
  const ConstPseudoVectorChar tz(tz_v);

  Rcpp::NumericVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    Global::dtime nano; memcpy(&nano, reinterpret_cast<const char*>(&e1_n[i]), sizeof(nano));
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e2_n[i]), sizeof(prd));
    auto dt = plus(nano, prd, Rcpp::as<std::string>(tz[i]));
    memcpy(&res[i], &dt, sizeof(dt));
  }
  copyNames(e1_nv, e2_cv, res);
  return assignS4("nanotime", res, "integer64");
}

// [[Rcpp::export]]
Rcpp::NumericVector minus_nanotime_period_impl(const Rcpp::NumericVector   e1_nv,
                                               const Rcpp::ComplexVector   e2_cv,
                                               const Rcpp::CharacterVector tz_v) {
  checkVectorsLengths(e1_nv, e2_cv);
  checkVectorsLengths(e1_nv, tz_v);
  checkVectorsLengths(e2_cv, tz_v);
  const ConstPseudoVectorNano e1_n(e1_nv);
  const ConstPseudoVectorPrd  e2_n(e2_cv);
  const ConstPseudoVectorChar tz(tz_v);

  Rcpp::NumericVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    Global::dtime nano; memcpy(&nano, reinterpret_cast<const char*>(&e1_n[i]), sizeof(nano));
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e2_n[i]), sizeof(prd));
    auto dt = minus(nano, prd, Rcpp::as<std::string>(tz[i % tz.size()]));
    memcpy(&res[i], &dt, sizeof(dt));
  }
  copyNames(e1_nv, e2_cv, res);
  return assignS4("nanotime", res, "integer64");
}

// [[Rcpp::export]]
Rcpp::ComplexVector plus_nanoival_period_impl(const Rcpp::ComplexVector   e1_cv,
                                              const Rcpp::ComplexVector   e2_cv,
                                              const Rcpp::CharacterVector tz_v) {
  checkVectorsLengths(e1_cv, e2_cv);
  checkVectorsLengths(e1_cv, tz_v);
  checkVectorsLengths(e2_cv, tz_v);
  const ConstPseudoVectorIval e1_n (e1_cv);
  const ConstPseudoVectorPrd  e2_n (e2_cv);
  const ConstPseudoVectorChar tz(tz_v);

  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    interval ival; memcpy(&ival, reinterpret_cast<const char*>(&e1_n[i]), sizeof(ival));
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e2_n[i]), sizeof(prd));
    auto res_ival = plus(ival, prd, Rcpp::as<std::string>(tz[i % tz.size()]));
    memcpy(&res[i], &res_ival, sizeof(res_ival));
  }
  copyNames(e1_cv, e2_cv, res);
  return assignS4("nanoival", res);
}

// [[Rcpp::export]]
Rcpp::ComplexVector minus_nanoival_period_impl(const Rcpp::ComplexVector   e1_cv,
                                               const Rcpp::ComplexVector   e2_cv,
                                               const Rcpp::CharacterVector tz_v) {
  checkVectorsLengths(e1_cv, e2_cv);
  checkVectorsLengths(e1_cv, tz_v);
  checkVectorsLengths(e2_cv, tz_v);
  const ConstPseudoVectorIval e1_n (e1_cv);
  const ConstPseudoVectorPrd  e2_n (e2_cv);
  const ConstPseudoVectorChar tz(tz_v);
  
  Rcpp::ComplexVector res(std::max(e1_n.size(), e2_n.size()));
  for (R_xlen_t i=0; i<res.size(); ++i) {
    interval ival; memcpy(&ival, reinterpret_cast<const char*>(&e1_n[i]), sizeof(ival));
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e2_n[i]), sizeof(prd));
    auto res_ival = minus(ival, prd, Rcpp::as<std::string>(tz[i % tz.size()]));
    memcpy(&res[i], &res_ival, sizeof(res_ival));
  }
  copyNames(e1_cv, e2_cv, res);
  return assignS4("nanoival", res);
}


// [[Rcpp::export]]
Rcpp::NumericVector period_month_impl(const Rcpp::ComplexVector e_n) {
  Rcpp::NumericVector res(e_n.size());
  for (R_xlen_t i=0; i<e_n.size(); ++i) {
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e_n[i]), sizeof(period));
    if (prd.isNA()) {
      res[i] = NA_REAL;
    }
    else {
      res[i] = prd.getMonths();
    }
  }
  if (e_n.hasAttribute("names")) {
    res.names() = e_n.names();
  }
  return res;
}


// [[Rcpp::export]]
Rcpp::NumericVector period_day_impl(const Rcpp::ComplexVector e_n) {
  Rcpp::NumericVector res(e_n.size());
  for (R_xlen_t i=0; i<e_n.size(); ++i) {
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e_n[i]), sizeof(period));
    if (prd.isNA()) {
      res[i] = NA_REAL;
    }
    else {
      res[i] = prd.getDays();
    }
  }
  if (e_n.hasAttribute("names")) {
    res.names() = e_n.names();
  }
  return res;
}


// [[Rcpp::export]]
Rcpp::S4 period_duration_impl(const Rcpp::ComplexVector e_n) {
  Rcpp::NumericVector res(e_n.size());
  for (R_xlen_t i=0; i<e_n.size(); ++i) {
    period prd; memcpy(&prd, reinterpret_cast<const char*>(&e_n[i]), sizeof(period));
    if (prd.isNA()) {
      auto dur = Global::duration::min();
      memcpy(&res[i], &dur, sizeof(dur));
    }
    else {
      auto dur = prd.getDuration();
      memcpy(&res[i], &dur, sizeof(dur));
    }
  }
  if (e_n.hasAttribute("names")) {
    res.names() = e_n.names();
  }
  return assignS4("nanoduration", res, "integer64");
}


// [[Rcpp::export]]
Rcpp::LogicalVector period_isna_impl(const Rcpp::ComplexVector cv) {
  Rcpp::LogicalVector res(cv.size());
  for (R_xlen_t i=0; i<cv.size(); ++i) {
    period prd;
    Rcomplex c = cv[i];
    memcpy(&prd, reinterpret_cast<const char*>(&c), sizeof(c));
    res[i] = prd.isNA();
  }
  res.names() = cv.names();
  return res;
}


constexpr Global::duration abs(Global::duration d) {
  return d >= d.zero() ? d : -d;
}

// [[Rcpp::export]]
Rcpp::NumericVector period_seq_from_to_impl(const Rcpp::NumericVector from_nv,
                                            const Rcpp::NumericVector to_nv,
                                            const Rcpp::ComplexVector by_cv,
                                            const std::string tz) {
  const ConstPseudoVectorNano from_n(from_nv);
  const ConstPseudoVectorNano to_n(to_nv);
  const ConstPseudoVectorPrd  by_n(by_cv);
  Global::dtime from; memcpy(&from, reinterpret_cast<const char*>(&from_n[0]), sizeof(from));
  Global::dtime to;   memcpy(&to,   reinterpret_cast<const char*>(&to_n[0]),   sizeof(to));
  period by;          memcpy(&by,   reinterpret_cast<const char*>(&by_n[0]),   sizeof(by));

  std::vector<Global::dtime> res{from};

  auto diff = to - from;
  auto pos = diff >= std::chrono::seconds(0);
  auto dist = abs(diff);
  auto olddist = dist;
  for (;;) {
    auto next = plus(res.back(), by, tz);
    if (pos ? next > to : next < to) break;
    res.push_back(next);
    olddist = dist;
    dist = abs(to - next);
    if (dist >= olddist) {
      Rcpp::stop("incorrect specification for 'to'/'by'"); // # nocov
    }
  }

  Rcpp::NumericVector res_rcpp(res.size());
  memcpy(&res_rcpp[0], &res[0], sizeof(Global::dtime)*res.size());
  return assignS4("nanotime", res_rcpp, "integer64");
}

// [[Rcpp::export]]
Rcpp::NumericVector period_seq_from_length_impl(const Rcpp::NumericVector from_nv,
                                                const Rcpp::ComplexVector by_cv,
                                                const Rcpp::NumericVector n_nv,
                                                const std::string tz) {
  const ConstPseudoVectorNano from_n(from_nv);
  const ConstPseudoVectorPrd  by_n(by_cv);
  const ConstPseudoVectorNano n_n(n_nv);

  Global::dtime from; memcpy(&from, reinterpret_cast<const char*>(&from_n[0]), sizeof(from));
  period by;          memcpy(&by,   reinterpret_cast<const char*>(&by_n[0]),   sizeof(by));
  size_t n;           memcpy(&n,    reinterpret_cast<const char*>(&n_n[0]),    sizeof(n));

  std::vector<Global::dtime> res{from};

  for (size_t i=1; i<n; ++i) {
    res.push_back(plus(res[i-1], by, tz));
  }

  Rcpp::NumericVector res_rcpp(res.size());
  memcpy(&res_rcpp[0], &res[0], sizeof(Global::dtime)*res.size());
  return assignS4("nanotime", res_rcpp, "integer64");
}