/* Routines for using cycle counter */

/* Start the counter */
void start_counter();

/* Get # cycles since counter started */
double get_counter();


void start_timer();
long int get_timer();


/* Measure overhead for counter */
double ovhd();

/* Determine clock rate of processor */
double mhz(int verbose);

/* Determine clock rate of processor, having more control over accuracy */
double mhz_full(int verbose, int sleeptime);

/** Special counters that compensate for timer interrupt overhead */

void start_comp_counter();

double get_comp_counter();
