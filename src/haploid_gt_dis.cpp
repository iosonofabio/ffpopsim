/*
 * haploid_gt_dis.cpp
 *
 *  Created on: Jan 27, 2010
 *  Author: Richard Neher & Boris Shraiman
 *  Modified by: Fabio Zanini
 */
#include "popgen_lowd.h"

/**
 * @brief Default constructor
 *
 * It prepares a few parameters, does not allocate memory (see set_up for that).
 */
haploid_gt_dis::haploid_gt_dis() {
	mem=false;
	free_recombination=true;
	outcrossing_rate=0.0;
	generation=0;
	long_time_generation=0.0;
	circular=false;
	number_of_loci=0;
}

/**
 * @brief Default destructor
 *
 * Release memory.
 */
haploid_gt_dis::~haploid_gt_dis() {
	free_mem();
}

/**
 * @brief Constructor + set_up
 *
 * @param L_in number of loci
 * @param N_in population size
 * @param rngseed seed for the random number generator. If this is zero, time(NULL)+getpid() is used.
 */
haploid_gt_dis::haploid_gt_dis(int L_in, double N_in, int rngseed) {
	mem=false;
	free_recombination=true;
	outcrossing_rate=0.0;
	generation=0;
	long_time_generation=0.0;
	circular=false;
	set_up(L_in, N_in, rngseed);
}

/**
 * @brief Construct a population with certain parameters.
 *
 * @param L_in number of loci
 * @param N_in number of individuals
 * @param rng_seed seed for the random number generator. If this is zero, time(NULL)+getpid() is used.
 *
 * @returns zero if successful, error codes otherwise
 *
 * Note: memory allocation is also performed here, via the allocate_mem function.
 */
int haploid_gt_dis::set_up(int L_in, double N_in, int rngseed) {
	population_size=N_in;
	number_of_loci=L_in;

	//In case no seed is provided use current second and add process ID
	if (rngseed==0)
		seed=time(NULL)+getpid();
	else
		seed=rngseed;

	return allocate_mem();
}

/**
 * @brief Allocate all the necessary memory, initialze the RNG.
 *
 * @returns zero if successful, error codes otherwise
 *
 * Set up the different hypercubes needed to store the fitness, population recombinants, and mutants.
 */
int haploid_gt_dis::allocate_mem() {
	int err=0;
	rng=gsl_rng_alloc(RNG);
	gsl_rng_set(rng, seed);
	err+=fitness.set_up(number_of_loci, gsl_rng_uniform_int(rng, gsl_rng_max(rng)));
	err+=population.set_up(number_of_loci, gsl_rng_uniform_int(rng, gsl_rng_max(rng)));
	err+=mutants.set_up(number_of_loci, gsl_rng_uniform_int(rng, gsl_rng_max(rng)));
	err+=recombinants.set_up(number_of_loci, gsl_rng_uniform_int(rng, gsl_rng_max(rng)));
	mutation_rates=new double*[2]; //allocate backward and forward mutation rates arrays
	mutation_rates[0]=new double [number_of_loci]; //allocate forward mutation rates
	mutation_rates[1]=new double [number_of_loci]; //allocate backward mutation rates
	for (int fb=0; fb<2; fb++){	//set mutation rates to zero
		for (int locus=0; locus<number_of_loci; locus++){
			mutation_rates[fb][locus]=0;
		}
	}
	if (err==0) {
		mem=true;
		return 0;
	}
	else return HG_MEMERR;
}


/**
 * @brief Releases memory during class destruction.
 *
 * @returns zero if successful, error codes otherwise
 */
int haploid_gt_dis::free_mem() {
	if (!mem) {
		cerr <<"haploid_gt_dis::free_mem(): No memory allocated!\n";
		return HG_BADARG;
	} else {
		fitness.~hypercube();
		population.~hypercube();
		recombinants.~hypercube();
		mutants.~hypercube();
		gsl_rng_free(rng);
		if (!free_recombination){
			for (int i=0; i<(1<<number_of_loci); i++){
				delete [] recombination_patters[i];
			}
			delete [] recombination_patters;
		}
		delete [] mutation_rates[1];
		delete [] mutation_rates[0];
		delete [] mutation_rates;
		mem=false;
		return 0;
	}
}

/**
 * @brief Initialize population in linkage equilibrium.
 *
 * @param nu target allele frequencies
 *
 * @returns zero if successful, error codes otherwise
 *
 * Note: when this function is used to initialize the population, it is likely that the fitness distribution
 * has a very large width. In turn, this can result in an immediate and dramatic drop in diversity within the
 * first few generations. Please check fitness statistics before starting the evolution if this worries you.
 */
int haploid_gt_dis::init_frequencies(double *freq) {
	double prob;
	int locus, i;
	population.set_state(HC_FUNC);
	for (i=0; i<(1<<number_of_loci); i++){
		prob=1.0;
		for (locus=0; locus<number_of_loci; locus++){
			if (i&(1<<locus)) prob*=freq[locus];
			else prob*=(1.0-freq[locus]);
		}
		population.func[i]=prob;
	}
	generation=0;
	long_time_generation=0.0;
	return population.fft_func_to_coeff();
}

/**
 * @brief Initialize the population with specific genotype frequencies
 *
 * @param gt vector of index_value_pair_t type with indices and frequencies
 *
 * @returns zero if successful, error codes otherwise
 */
int haploid_gt_dis::init_genotypes(vector <index_value_pair_t> gt) {
	population.init_list(gt, false);
	generation=0;
	long_time_generation=0.0;
	return population.normalize();
}

/**
 * @brief Evolve the population for some generations
 *
 * @param gen number of generations
 *
 * @returns zero if successful, error code in the faulty step otherwise
 *
 * *Note*: the order of selection, mutation, recombination, and resampling could be changed
 * according to needs and beliefs. Note that only recombination calculates the inverse
 * fourier transform of the population. It does so BEFORE the recombination step.
 * To evaluate all allele frequencies and linkage disequilibria, call population.fft_func_to_coeff()
 */
int haploid_gt_dis::evolve(int gen) {
	if (HG_VERBOSE) cerr<<"haploid_gt_dis::evolve(int gen)...";
	int err=0, g=0;

	// evolve cycle
	while((err == 0) && (g < gen)) {
		if (HG_VERBOSE) cerr<<"generation "<<generation<<endl;
		if(err==0) err=select();
		if(err==0) err=mutate();
		if(err==0) err=recombine();
		if(err==0) err=resample();
		g++;
		generation++;
		if (generation>HG_LONGTIMEGEN) {generation-=HG_LONGTIMEGEN; long_time_generation+=HG_LONGTIMEGEN;}
	}
	if (HG_VERBOSE) {
		if(err==0) cerr<<"done."<<endl;
		else cerr<<"error "<<err<<"."<<endl;
	}
	return err;
}

/**
 * @brief Evolve the population for some generations, without recombination
 *
 * @param gen number of generations
 *
 * @returns zero if successful, error code in the faulty step otherwise
 *
 * *Note*: the order of selection, mutation, and resampling could be changed
 * according to needs and beliefs.
 */
int haploid_gt_dis::evolve_norec(int gen) {
	if (HG_VERBOSE) cerr<<"haploid_gt_dis::evolve_norec(int gen)...";
	int err=0, g=0;

	// evolve cycle
	while((err == 0) && (g < gen)) {
		if (HG_VERBOSE) cerr<<"generation "<<generation<<endl;
		if(err==0) err=select();
		if(err==0) err=mutate();
		if(err==0) err=resample();
		g++;
		generation++;
		if (generation>HG_LONGTIMEGEN) {generation-=HG_LONGTIMEGEN; long_time_generation+=HG_LONGTIMEGEN;}
	}
	if (HG_VERBOSE) {
		if(err==0) cerr<<"done."<<endl;
		else cerr<<"error "<<err<<"."<<endl;
	}
	return err;
}

/**
 * @brief Evolve the population for some generations, without resampling (deterministic)
 *
 * @param gen number of generations
 *
 * @returns zero if successful, error code in the faulty step otherwise
 *
 * *Note*: the order of selection, mutation, and recombination could be changed
 * according to needs and beliefs. Note that only recombination calculates the inverse
 * fourier transform of the population. It does so BEFORE the recombination step.
 * To evaluate all allele frequencies and linkage disequilibria, call population.fft_func_to_coeff()
 */
int haploid_gt_dis::evolve_deterministic(int gen) {
	if (HG_VERBOSE) cerr<<"haploid_gt_dis::evolve(int gen)...";
	int err=0, g=0;

	// evolve cycle
	while((err == 0) && (g < gen)) {
		if (HG_VERBOSE) cerr<<"generation "<<generation<<endl;
		if(err==0) err=select();
		if(err==0) err=mutate();
		if(err==0) err=recombine();
		g++;
		generation++;
		if (generation>HG_LONGTIMEGEN) {generation-=HG_LONGTIMEGEN; long_time_generation+=HG_LONGTIMEGEN;}
	}
	if (HG_VERBOSE) {
		if(err==0) cerr<<"done."<<endl;
		else cerr<<"error "<<err<<"."<<endl;
	}
	return err;
}

/**
 * @brief Selection step
 *
 * @returns zero if successful, nonzero otherwise
 *
 * *Note*: Population distribution is reweighted with exp(fitness) and renormalized.
 */
int haploid_gt_dis::select() {
	// FIXME: nobody should set the state from the outside like this...check whether we can save the elegance without losing speed!
	population.set_state(HC_FUNC);
	double norm=0;
	for (int i=0; i<(1<<number_of_loci); i++){
		population.func[i]*=exp(fitness.func[i]);
		norm+=population.func[i];
	}
	population.scale(1.0/norm);
	return 0;
}

/**
 * @brief Resample the population to reduce the size to approximately n discrete individuals
 *
 * @param n desired population size
 *
 * @returns zero if successful, error codes otherwise
 *
 * *Note*: genotypes with few individuals are sampled using the Poisson distribution, allowing for strict zero;
 * genotypes with many individuals are resampled using a Gaussian distribution, for performance reasons.
 */
int haploid_gt_dis::resample(double n) {
	double pop_size;
	if (n<1.0) pop_size=population_size;
	else pop_size=n;

	population.set_state(HC_FUNC);
	double threshold_HG_CONTINUOUS=double(HG_CONTINUOUS)/pop_size, norm;
	norm=0;
	for (int i=0; i<(1<<number_of_loci); i++){
		if (population.func[i]<threshold_HG_CONTINUOUS)
		{
			population.func[i]=double(gsl_ran_poisson(rng, pop_size*population.func[i]))/pop_size;
		}
		else
		{
			population.func[i]+=double(gsl_ran_gaussian(rng, sqrt(population.func[i]/pop_size)));
		}
		norm+=population.func[i];
	}
	if (norm<HG_NOTHING){
		return HG_EXTINCT;
	}
	else population.scale(1.0/norm);
	return 0;
}

/**
 * @brief Mutation step
 *
 * @returns zero if successful, nonzero otherwise
 *
 * Calculate the distribution of mutants and update the population distribution
 */
int haploid_gt_dis::mutate() {
	int locus;
	mutants.set_state(HC_FUNC);
	population.set_state(HC_FUNC);
	for (int i=0; i<(1<<number_of_loci); i++){
		mutants.func[i]=0;
		for (locus=0; locus<number_of_loci; locus++)
		{
			if (i&(1<<locus)){
				mutants.func[i]+=mutation_rates[0][locus]*population.func[i-(1<<locus)]-mutation_rates[1][locus]*population.func[i];
			}else{
				mutants.func[i]+=mutation_rates[1][locus]*population.func[i+(1<<locus)]-mutation_rates[0][locus]*population.func[i];
			}
		}
	}
	for (int i=0; i<(1<<number_of_loci); i++){
		population.func[i]+=mutants.func[i];
		//cout <<mutation_rate<<"  "<<mutants.func[i]<<"  "<<population.func[i]<<endl;
	}
	return 0;
}

/**
 * @brief Recombination step
 *
 * @returns zero if successful, error codes otherwise
 *
 * FIXME: is the following paragraph correct?
 * Calculate the distribution of recombinants and update the population, in case of free
 * recombinations, a fraction is replaced (outcrossing_rate), in case of general recombination
 * the entire population is replaced, i.e. obligate mating.
 */
int haploid_gt_dis::recombine() {
	int err;
	err=calculate_recombinants();
	population.set_state(HC_FUNC);
	if (free_recombination){
		for (int i=0; i<(1<<number_of_loci); i++){
			population.func[i]+=outcrossing_rate*(recombinants.func[i]-population.func[i]);
		}
	}else{
		for (int i=0; i<(1<<number_of_loci); i++){
			population.func[i]=recombinants.func[i];
		}
	}
	return err;
}

/**
 * @brief (Proxy function) Call the appropriate recombination routine
 *
 * @returns the return value of the underlying routine
 *
 * Possible choices are:
 * - calculate_recombinants_free();
 * - calculate_recombinants_general();
 *
 * FIXME: do we really need this, since recombine() also does some strange stuff afterwards?
 */
int haploid_gt_dis::calculate_recombinants() {
	if (free_recombination) return calculate_recombinants_free();
	else return calculate_recombinants_general();
}

/**
 * @brief Calculate the recombinant distribution for the free recombination case
 *
 * @returns zero if successful, nonzero otherwise
 *
 * Almost the same as for the more general case below, but kept separate for
 * performance reasons - this is the most expensive part (3^L).
 */
int haploid_gt_dis::calculate_recombinants_free() {
	int i,j,k, maternal_alleles, paternal_alleles, count;

	// prepare hypercubes
	population.fft_func_to_coeff();
	recombinants.set_state(HC_COEFF);

	//normalization of the distribution
	recombinants.coeff[0]=1.0/(1<<number_of_loci);

	//loop of all coefficients of the distribution of recombinants
	for (i=1; i<(1<<number_of_loci); i++) {
		recombinants.coeff[i]=0;

		//loop over all possible partitions of the loci s1..sk in R^(k)_s1..sk to mother and father
		for (j=0; j<(1<<recombinants.order[i]); j++) {
			count=0;
			maternal_alleles=0;
			paternal_alleles=0;

			//build the integers to pull out the maternal and paternal moments
			for (k=0; k<number_of_loci; k++)
				if (i&(1<<k)) {
					if (j&(1<<count)) maternal_alleles+=(1<<k);
					else paternal_alleles+=(1<<k);
					count++;
				}

			//add this particular contribution to the recombinant distribution
			recombinants.coeff[i]+=population.coeff[maternal_alleles]*population.coeff[paternal_alleles];
		}

		//normalize: the factor 1<<number_of_loci is due to a peculiarity of the fft algorithm
		recombinants.coeff[i]*=1.0*(1<<(number_of_loci-recombinants.order[i]));
	}

	//backtransform to genotype representation
	recombinants.fft_coeff_to_func();
	return 0;
}

/**
 * @brief Calculate the recombinant distribution for the general case
 *
 * @returns zero if successful, nonzero otherwise
 *
 * Calculate the distribution after recombination assumed in random mating with
 * pairs sampled with replacement.
 */
int haploid_gt_dis::calculate_recombinants_general() {
	int i,j,k, maternal_alleles, paternal_alleles, count;

	// prepare hypercubes
	population.fft_func_to_coeff();
	recombinants.set_state(HC_COEFF);

	//normalization of the distribution
	recombinants.coeff[0]=1.0/(1<<number_of_loci);
	if(HG_VERBOSE) cerr<<0<<"  "<<recombinants.coeff[0]<<endl;

	//loop of all coefficients of the distribution of recombinants
	for (i=1; i<(1<<number_of_loci); i++) {
		recombinants.coeff[i]=0;

		//loop over all possible partitions of the loci s1..sk in R^(k)_s1..sk to mother and father
		for (j=0; j<(1<<recombinants.order[i]); j++) {
			count=0;
			maternal_alleles=0;
			paternal_alleles=0;

			//build the integers to pull out the maternal and paternal moments
			for (k=0; k<number_of_loci; k++)
				if (i&(1<<k))
				{
					if (j&(1<<count)) maternal_alleles+=(1<<k);
					else paternal_alleles+=(1<<k);
					count++;
				}

			//add this particular contribution to the recombinant distribution
			recombinants.coeff[i]+=recombination_patters[i][j]*population.coeff[maternal_alleles]*population.coeff[paternal_alleles];
			if(HG_VERBOSE >= 2) cerr<<i<<"  "<<recombinants.coeff[i]<<"  "<<population.coeff[paternal_alleles]<<endl;
		}

		//normalize: the factor 1<<number_of_loci is due to a peculiarity of the the fft algorithm
		recombinants.coeff[i]*=(1<<(number_of_loci));
		if(HG_VERBOSE) cerr<<i<<"  "<<recombinants.coeff[i]<<endl;
	}

	//backtransform to genotype representation
	recombinants.fft_coeff_to_func();
	return 0;
}

/**** Set the mutation rate(s) in various ways ****/

/**
 * @brief Set a uniform mutation rate for all loci and both directions
 *
 * @param m mutation rate
 *
 * @returns zero if successful, error codes otherwise
 */
int haploid_gt_dis::set_mutation_rate(double m) {
	if (mem){
		for (int fb=0; fb<2; fb++){
			for (int locus=0; locus<number_of_loci; locus++){
				mutation_rates[fb][locus]=m;
			}
		}
		return 0;
	} else {
		cerr<<"haploid_gt_dis::set_mutation_rate(): allocate memory first!\n";
		return HG_MEMERR;
	}
}

/**
 * @brief Set two mutation rates (forward / backward) for all loci
 *
 * @param mforward forward mutation rate
 * @param mbackward backward mutation rate
 *
 * @returns zero if successful, error codes otherwise
 */
int haploid_gt_dis::set_mutation_rate(double mforward, double mbackward) {
	if (mem){
		for (int locus=0; locus<number_of_loci; locus++){
			mutation_rates[0][locus]=mforward;
			mutation_rates[1][locus]=mbackward;
		}
		return 0;
	} else {
		cerr<<"haploid_gt_dis::set_mutation_rate(): allocate memory first!\n";
		return HG_MEMERR;
	}
}

/**
 * @brief Set mutation rates (locus specific, both directions the same)
 *
 * @param m array of mutation rates
 *
 * @returns zero if successful, error codes otherwise
 */
int haploid_gt_dis::set_mutation_rate(double* m) {
	if (mem){
		for (int locus=0; locus<number_of_loci; locus++){
			mutation_rates[0][locus]=m[locus];
			mutation_rates[1][locus]=m[locus];
		}
		return 0;
	}else{
		cerr<<"haploid_gt_dis::set_mutation_rate(): allocate memory first!\n";
		return HG_MEMERR;
	}
}

/**
 * @brief Set mutation rates (locus and direction specific)
 *
 * @param m array of mutation rates
 *
 * @returns zero if successful, error codes otherwise
 */
int haploid_gt_dis::set_mutation_rate(double** m) {
	if (mem){
		for (int fb=0; fb<2; fb++){
			for (int locus=0; locus<number_of_loci; locus++){
				mutation_rates[fb][locus]=m[fb][locus];
			}
		}
		return 0;
	}else{
		cerr<<"haploid_gt_dis::set_mutation_rate(): allocate memory first!\n";
		return HG_MEMERR;
	}
}


/**
 * @brief calculate recombination patterns
 *
 * @param rec_rates a vector of recombination rates. The first entry should be large for linear chromosomes.
 *
 * @returns zero if successful, error codes otherwise (e.g. out of memory)
 *
 * A routine the calculates the probability of all possible recombination patters and
 * subpatterns thereof from a vector of recombination rates (rec_rates) passed as argument.
 * It allocated the memory (3^L) and calculates the entire distribution.
 *
 * The first entry is the recombination rate before the first locus, i.e. it should be large >50
 * for linear chromosomes. all other entries are recombination rates between successive loci.
 *
 */
int haploid_gt_dis::set_recombination_rates(double *rec_rates) {
	double err=0;
	int i, spin;
	//check whether the memory is already allocated, do so if not
	if (free_recombination==true)
	{
		int temp;
		int *nspins;	//temporary variables the track the number of ones in the binary representation of i
		nspins=new int [1<<number_of_loci];
		if (nspins==NULL) {
			cerr<<"haploid_gt_dis::set_recombination_rates(): Can not allocate memory!"<<endl;
			return HG_MEMERR;
		}
		spin=-1;
		nspins[0]=0;
		//allocate space for all possible subsets of loci
		recombination_patters=new double* [1<<number_of_loci];
		recombination_patters[0]=new double	[1];
		//loop over all possible locus subsets and allocate space for all
		//possible ways to assign the subset to father and mother (2^nspins)
		for (i=1; i<(1<<number_of_loci); i++){
			if (i==(1<<(spin+1))) spin++;
			temp=1+nspins[i-(1<<spin)];	//the order of coefficient k is 1+(the order of coefficient[k-2^spin])
			nspins[i]=temp;
			//all possible ways to assign the subset to father and mother (2^nspins)
			recombination_patters[i]=new double [(1<<temp)];
			if (recombination_patters[i]==NULL) err+=1;
		}
		delete [] nspins;
	}
	if (err==0){	//if memory allocation has been successful, calculate the probabilities of recombination
		int strand=0,newstrand, locus, set_size, i, subset, rec_pattern, marg_locus, higher_order_subset, higher_order_rec_pattern;
		double *rptemp;
		double sum=0;
		int strandswitches;
		//calculate the probabilities of different cross over realizations
		//the constrained of even number of crossovers is fulfilled automatically
		for (i=0; i<(1<<number_of_loci); i++){
			recombination_patters[(1<<number_of_loci)-1][i]=1.0;
			strand=(i&(1<<(number_of_loci-1)))>0?1:0;
			strandswitches=0;
			for (locus=0; locus<number_of_loci; locus++)
			{
				newstrand=((i&(1<<locus))>0)?1:0;
				if (strand==newstrand) recombination_patters[(1<<number_of_loci)-1][i]*=(0.5*(1.0+exp(-2.0*rec_rates[locus])));
				else {
					recombination_patters[(1<<number_of_loci)-1][i]*=(0.5*(1.0-exp(-2.0*rec_rates[locus])));
					strandswitches++;
				}
				strand=newstrand;
			}
			if (strandswitches%2) recombination_patters[(1<<number_of_loci)-1][i]=0;
			sum+=recombination_patters[(1<<number_of_loci)-1][i];
		}
		for (i=0; i<(1<<number_of_loci); i++){
			recombination_patters[(1<<number_of_loci)-1][i]/=sum;
		}
		//loop over set of spins of different size, starting with 11111101111 type patters
		//then 11101110111 type patterns etc. first loop is over different numbers of ones, i.e. spins
		for (set_size=number_of_loci-1; set_size>=0; set_size--)
		{
			//loop over all 2^L binary patterns
			for (subset=0; subset<(1<<number_of_loci); subset++)
			{
				//if correct number of ones... (its the same in every hypercube...)
				if (fitness.order[subset]==set_size)
				{
					marg_locus=-1; //determine the first zero, i.e. a locus that can be used to marginalize
					for (locus=0; locus<number_of_loci; locus++)
					{
						if ((subset&(1<<locus))==0)
							{marg_locus=locus; break;}
					}
					//a short hand for the higher order recombination pattern, from which we will marginalize
					higher_order_subset=subset+(1<<marg_locus);
					rptemp=recombination_patters[higher_order_subset];
					//loop over all pattern of the length set_size and marginalize
					//i.e. 111x01011=111001011+111101011
					for (rec_pattern=0; rec_pattern<(1<<set_size); rec_pattern++){
						higher_order_rec_pattern=(rec_pattern&((1<<marg_locus)-1))+((rec_pattern&((1<<set_size)-(1<<marg_locus)))<<1);
						recombination_patters[subset][rec_pattern]=rptemp[higher_order_rec_pattern]+rptemp[higher_order_rec_pattern+(1<<marg_locus)];
					}
				}
			}
		}
		free_recombination=false;
		return 0;
	}else{
		cerr <<"haploid_gt_dis::set_recombination_rates(): cannot allocate memory for recombination patterns!"<<endl;
		return HG_MEMERR;
	}
}

/**
 * @brief Get the genotype entropy
 *
 * @returns the genotype entropy of the population
 *
 * The genotype entropy is defined as follows:
 * \f[ S := - \sum_{g} \nu_g \cdot \log \nu_g, \f]
 * where \f$g\f$ runs over all possible genomes, and \f$\nu_g\f$ is the frequency of that genome
 * in the population.
 */
double haploid_gt_dis::genotype_entropy(){
	double S=0;
	if (population.get_state()==HC_COEFF) population.fft_coeff_to_func();
	for (int i=0; i<(1<<number_of_loci); i++){
		S-=population.func[i]*log(population.func[i]);
	}
	return S;
}

/*
 * calculate the allele entropy and return
 * it has be made sure that the population.fft_func_to_coeff() was called
 */
/**
 * @brief Get the allele entropy
 *
 * @returns the allele entropy of the population
 *
 * The allele entropy is defined as follows:
 * \f[ S := - \sum_{i=1}^L \frac{1 + \nu_i}{2} \log \frac{1 + \nu_i}{2} + \frac{1 - \nu_i}{2} \log \frac{1 - \nu_i}{2}, \f]
 * where \f$\nu_i\f$ is the frequency of the i-th allele.
 */
double haploid_gt_dis::allele_entropy(){
	double SA=0;
	if (population.get_state()==HC_FUNC) population.fft_func_to_coeff();
	for (int locus=0; locus<number_of_loci; locus++){
		SA-=0.5*(1.0+population.coeff[(1<<locus)])*log(0.5*(1.0+population.coeff[(1<<locus)]));
		SA-=0.5*(1.0-population.coeff[(1<<locus)])*log(0.5*(1.0-population.coeff[(1<<locus)]));
	}
	return SA;
}

/**
 * @brief Get fitness mean and variance in the population
 *
 * @returns stat_t with the requested statistics
 */
stat_t haploid_gt_dis::get_fitness_statistics(){
	double mf=0, sq=0, temp;
	if (population.get_state()==HC_COEFF) population.fft_coeff_to_func();
	for (int locus=0; locus<1<<number_of_loci; locus++){
		temp=population.get_func(locus)*fitness.get_func(locus);
		mf+=temp;
		sq+=temp*temp;
	}
	return stat_t(mf, sq-mf);
}


/**
 * @brief Test the recombination routine using Fourier transforms
 *
 * @returns zero if both routines agree, -1 otherwise
 *
 * Debugging routine: calculates the distribution of recombinants explicitly and
 * compares the result to the recombinant distribution obtained via fourier transform
 */
int haploid_gt_dis_test::test_recombinant_distribution(){
	double *test_rec;
	double dev=0;
	//allocate memory for the recombinant distribution calculated step-by-step
	test_rec=new double [(1<<number_of_loci)];
	int mother, father;
	//calculate recombinants the efficient way
	calculate_recombinants();
	//now calculate the recombinant distribution from pairs of parents.
	int gt1, gt2, rec_pattern;
	if (free_recombination){
		for (gt1=0; gt1<(1<<number_of_loci); gt1++){	//target genotype
			test_rec[gt1]=0.0;							//initialize
			//loop over all recombination patters (equal probability)
			for (rec_pattern=0; rec_pattern<(1<<number_of_loci); rec_pattern++){
				//loop over the parts of the maternal and paternal genomes not inherited
				for (gt2=0; gt2<(1<<number_of_loci); gt2++){
					//construct maternal and paternal genotypes
					mother=(gt1&(rec_pattern))+(gt2&(~rec_pattern));
					father=(gt1&(~rec_pattern))+(gt2&(rec_pattern));
					//increment the rec distribution
					test_rec[gt1]+=population.func[mother]*population.func[father];
				}
			}
			//normalize
			test_rec[gt1]*=1.0/(1<<number_of_loci);
			cout <<gt1<<"  "<<test_rec[gt1]<<"  "<<recombinants.func[gt1]<<endl;
			//sum up all deviations
			dev+=(test_rec[gt1]-recombinants.func[gt1])*(test_rec[gt1]-recombinants.func[gt1]);
		}
	}else{ 	//same as above, only the individual contribution
		for (gt1=0; gt1<(1<<number_of_loci); gt1++){
			test_rec[gt1]=0.0;
			for (rec_pattern=0; rec_pattern<(1<<number_of_loci); rec_pattern++){
				for (gt2=0; gt2<(1<<number_of_loci); gt2++){
					mother=(gt1&(rec_pattern))+(gt2&(~rec_pattern));
					father=(gt1&(~rec_pattern))+(gt2&(rec_pattern));
					//contribution is weighted by the probability of this particular recombination pattern
					//this got calculated and stored in recombination_patters[(1<<number_of_loci)-1]
					test_rec[gt1]+=recombination_patters[(1<<number_of_loci)-1][rec_pattern]*population.func[mother]*population.func[father];
				}
			}
			cout <<gt1<<"  "<<test_rec[gt1]<<"  "<<recombinants.func[gt1]<<endl;
			dev+=(test_rec[gt1]-recombinants.func[gt1])*(test_rec[gt1]-recombinants.func[gt1]);
		}
	}
	delete [] test_rec;
	if (dev>1e-9){
		cout <<"Deviation between explicit and fourier transform version! "<<dev<<endl;
		return -1;
	}else{
		cout <<"Explicit and fourier transform version agree to "<<dev<<endl;
		return 0;
	}
	return 0;
}

/**
 * @brief Test the recombination routine extensively
 *
 * @param rec_rates recombination rates used for testing
 *
 * @returns zero (but look at the stdout)
 *
 * Debugging routine: produces random genotypes configurations and test whether they recombine correctly.
 */
int haploid_gt_dis_test::test_recombination(double *rec_rates){

	//calculate the genetic map, i.e. cumulative recombination rates
	double* cumulative_rates=new double [number_of_loci+1];
	cumulative_rates[0]=0.0;
	for (int locus =1; locus<number_of_loci+1; locus++) cumulative_rates[locus]=cumulative_rates[locus-1]+rec_rates[locus-1];

	//initialize the internal recombination rates
	set_recombination_rates(rec_rates);

	//initialize the population randomly and test the recombination procedure
	population.set_state(HC_FUNC);
	for (int r=0; r<1; r++){
		for (int i=0; i<(1<<number_of_loci); i++){
			population.func[i]=gsl_rng_uniform(rng);
		}
		population.normalize();
		test_recombinant_distribution();
	}
	population.set_state(HC_FUNC);
	for (int i=0; i<(1<<number_of_loci); i++){
		population.func[i]=gsl_rng_uniform(rng);
	}
	population.normalize();

	//study the decay of cumulants from the randomly initialized initialized population
	//output header
	cout <<"\n\nRatio of the cumulants and the expected decay curve, should be constant. Last column shows dynamic range\n";
	cout <<"Generation  ";
	for (int l1=0; l1<number_of_loci; l1++){
		for(int l2=0; l2<l1; l2++){
			cout <<setw(13)<<l1<<" "<<l2;
		}
	}
	cout <<setw(15)<<"exp(-rmax*t)";
	cout<<'\n';
	//for a thousand time steps, recombine and watch the cumulants decay
	for (int g=0; g<1000; g++){
		if (g%100==0){ //output every hundred generations
			cout <<setw(10)<<g;
			for (int l1=0; l1<number_of_loci; l1++){
				for(int l2=0; l2<l1; l2++){
					cout <<setw(15)<<get_LD(l1,l2)*exp(g*0.5*(1.0-exp(-2.0*(cumulative_rates[l1+1]-cumulative_rates[l2+1]))));
				}
			}
			cout <<setw(15)<<exp(-g*0.5*(1.0-exp(-2.0*(cumulative_rates[number_of_loci]-cumulative_rates[1]))));
			cout<<'\n';
		}
		recombine();
	}
	delete cumulative_rates;
	return 0;
}



/**
 * @brief Test the mutation-drift equilibrium with diffusion theory
 *
 * @param mu mutation rates
 *
 * @returns zero (but look at the stdout)
 */
int haploid_gt_dis_test::mutation_drift_equilibrium(double **mu){
	set_mutation_rate(mu);

	//init population and recombination rates
	double *af=new double[number_of_loci];;
	double *recrates=new double[number_of_loci];;
	for (int i=0; i<number_of_loci; i++){
		af[i]=0;
		recrates[i]=10;
	}
	init_frequencies(af);
	//allocate histograms to store allele frequency distributions
	gsl_histogram **mutfreq=new gsl_histogram* [number_of_loci];
	for (int locus=0; locus<number_of_loci; locus++){
		mutfreq[locus]=gsl_histogram_alloc(100);
		gsl_histogram_set_ranges_uniform(mutfreq[locus], -1,1);
	}

	//equilibrate for 2N generations
	for (int gen=0; gen<2*population_size; gen++){
		mutate();
		resample();
	}
	//take 100000 samples every 1000 generations (assumes population is of order 1000)
	for (int r=0; r<100000; r++){
		for (int gen=0; gen<1000; gen++){
			mutate();
			resample();
		}

		for (int locus=0; locus<number_of_loci; locus++){
			gsl_histogram_increment(mutfreq[locus], get_chi(locus));
		}
	}

	//output: normalized histograms as well as theoretical expectation from diffusion theory
	//calculate norm of distributions first, output below.
	double upper, lower;
	double* histogramnorm=new double [number_of_loci];
	double* theorynorm=new double [number_of_loci];
	for (int locus=0; locus<number_of_loci; locus++){
		histogramnorm[locus]=0;
		theorynorm[locus]=0;
		for (int i=0; i<100; i++){
			gsl_histogram_get_range(mutfreq[locus], i, &lower, &upper);
			histogramnorm[locus]+=gsl_histogram_get(mutfreq[locus], i);
			theorynorm[locus]+=pow(0.5*(1+0.5*(upper+lower)), 2*population_size*mu[0][locus]-1)*pow(0.5*(1-0.5*(upper+lower)), 2*population_size*mu[1][locus]-1);
		}
	}
	for (int i=0; i<100; i++){
		gsl_histogram_get_range(mutfreq[0], i, &lower, &upper);
		cout <<setw(15)<<0.5*(upper+lower);
		for (int locus=0; locus<number_of_loci; locus++){
			cout <<setw(15)<<gsl_histogram_get(mutfreq[locus], i)/histogramnorm[locus]
					<<setw(15)<<pow(0.5*(1+0.5*(upper+lower)), 2*population_size*mu[0][locus]-1)*pow(0.5*(1-0.5*(upper+lower)), 2*population_size*mu[1][locus]-1)/theorynorm[locus];
		}
		cout <<endl;
	}
	return 0;
}
