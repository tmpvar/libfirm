#ifndef __FIRM_WIN32_TIME__
#define __FIRM_WIN32_TIME__
  typedef struct timeval {
      long tv_sec;
      long tv_usec;
  } timeval;

  typedef struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
  } timezone;

  int gettimeofday(struct timeval * tp, struct timezone * tzp);
#endif