//
//      Copyright (C) 1996-2011, Wideband Computers, Inc.
//      All Rights Reserved
//
//		Author: 		D. Isaksen
//		Last Update:	16-May-2011
//
//      Compute a Baseband Signal for Hittite Demodulator:
//
//	    This tool performs the following:
//
//			1. Generates random I and Q symbol values
//
//			2. Nyquist filters the random symbols
//
//			3. Performs a specified signal impairment
//			   a. DC offsets
//			   b. Gain imbalance
//			   c. Phase imbalance
//
//			3. Adds a specified Gaussian noise to the symbols
//
//			4. Store the resultant I and Q signal outputs in a file
//
//		The program is invoked as:
//
//			hittite_siggen
//
//		The Nyquist coefficient are generated by another tool
//
//		The DC offsets, Gain imbalance, and Phase imbalance can be specified in this tool
//
//		The standard deviation of the Gaussian noise can be specified.
//
//

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits.h>
#include <math.h>
#include <vector>
#include <boost/circular_buffer.hpp>
#include <numeric>
#include <tr1/cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

typedef	int32_t	intSigGen;
using namespace std;

void	wci_get_qpsk  ( intSigGen *i_symb, intSigGen *q_symb ) ;
void	wci_get_gauss ( double *noise) ;
void	wci_nyq_filt  ( intSigGen i_symb,
						intSigGen q_symb,
						vector<intSigGen> &nyq_coeffs,
						vector<intSigGen> &nyq_delay_i,
						vector<intSigGen> &nyq_delay_q,
						intSigGen *nyq_iout,
						intSigGen *nyq_qout) ;
void get_rs_coeffs (	intSigGen *p,			// Pointer to the first coefficient
						intSigGen  resamp_index,	// Delay index
						vector<intSigGen> &res_coeffs	// Array of current 9 resampler coefficients
						) ;
void wci_nyq_filt_boost ( intSigGen i_symb,				// I symbol input
		intSigGen q_symb,				// Q symbol input
		vector<intSigGen> &nyq_coeffs,
		boost::circular_buffer<intSigGen> &nyq_delay_i,		// I delay line storage
		boost::circular_buffer<intSigGen> &nyq_delay_q,		// Q delay line storage
		intSigGen *nyq_iout,	 		// I output from Nyquist filter
		intSigGen *nyq_qout);			// Q output from Nyquist filter

const	intSigGen	NUM_SYMBOLS = 1 << 10 ;
const	intSigGen	COEFF_LEN 	= 256 * 8 ;
const	intSigGen	NBR_RES_COEFFS = 9;


bool load_from_file(const string &filename, std::vector<intSigGen> &coeff);//coeff must have the correct size

int main (int argc, char* argv[])
{
	srand(1);

	time_t startTime, stopTime, elapsedTime;
	intSigGen	res_total_coeff[1152] ;

	intSigGen	i_symb ;
	intSigGen	q_symb ;

	vector<intSigGen>	nyq_coeffs(COEFF_LEN, 0) ;
	boost::circular_buffer <intSigGen>	nyq_delay_i(COEFF_LEN, 0);
	boost::circular_buffer <intSigGen>	nyq_delay_q(COEFF_LEN, 0);

	intSigGen	nyq_iout ;
	intSigGen	nyq_qout ;

	vector<intSigGen>	res_coeffs(NBR_RES_COEFFS, 0) ;
	boost::circular_buffer <intSigGen>	res_delay_i(NBR_RES_COEFFS, 0);
	boost::circular_buffer <intSigGen>	res_delay_q(NBR_RES_COEFFS, 0);

	intSigGen	res_iout ;
	intSigGen	res_qout ;
	intSigGen	resamp_index ;

	intSigGen	fgain ;

	double	temp ;

	double	i_noise ;
	double	q_noise ;

	double	i_symb_noise ;
	double	q_symb_noise ;

	double	isymb_temp ;
	double	qsymb_temp ;

	double	phase_offset ;

	double	snr ;
	double	snr_scale ;
	double	cPwr = 0.0, nPwr = 0.0;

	double	i_dc_offset ;
	double	q_dc_offset ;
	double	theta ;
	double	gain_imbalance ;

	double	yi_cos_theta ;
	double	yi_sin_theta ;
	double	yq_cos_theta ;
	double	yq_sin_theta ;
	double	yi_sum_phase ;
	double	yq_sum_phase ;

	double	zi ;
	double	zq ;

	string	filename;
	fstream fphex;
	fstream	fp_rscoeff ;
	fstream	fp_nyquist ;

	intSigGen	i ;
	intSigGen	j ;

	string	parmStr;

	startTime = time(NULL);		// for measuring runtime.

// argument definition:
// argv[1]: desired snr_scale
// argv[2]: fgain
	if (argc > 1)
		parmStr = string(argv[1]);
	else
		parmStr = "00";
	snr_scale = atoi(parmStr.c_str());
	cout << "snr_scale is: " << snr_scale << endl;
	filename = "data_output/hittite_qpsk" + parmStr + ".txt";
	fphex.open(filename.c_str(), fstream::out);

	if (argc > 2)
		parmStr = string(argv[2]);
	else
		parmStr = "12";
	fgain = 1 << atoi(parmStr.c_str());
	cout << "fgain is: " << fgain << endl;

	if( not load_from_file("coeff_nyquist_2048.dat", nyq_coeffs) )
		return -1;
	//if( not load_from_file("coeff_resamp_128.dat", res_total_coeff) )
		//return -1;
//	New Section to open and read the resampler coefficients
	filename = "coeff_resamp_128.dat";
	fp_rscoeff.open(filename.c_str(), fstream::in);
	if (!fp_rscoeff.good()) {
		cout << "Error: Can't open file " << filename << endl ;
		return -1;
	}

	for(j = 0; fp_rscoeff >> res_total_coeff[j]; ++j)
		;

	if ( j == 1152)
		cout << "RS Coeffs loaded" << endl;
	else
		cout << "RS Coeffs load failed" << endl ;

	resamp_index = 8 ;

	get_rs_coeffs (	&res_total_coeff[0],	// Pointer to the first coefficient
					resamp_index,			// Delay index
					res_coeffs			// Array of current 9 resampler coefficients
					) ;

//	End of new section to read and store the resampler coefficients
//	The coefficients for all 128 banks (9 coeffs per bank) are stored
//	in the array, res_total_coeff[1152]

	//	Initialize the DC offsets, gain imbalance and phase imbalance settings
	i_dc_offset = 0.0 ;							// 0 I DC offset
	q_dc_offset = 0.0 ;							// 0 I DC offset

	gain_imbalance = 0.0 ;						// 0 Gain imbalance
	theta = 0.0 * 3.14159 / 180.0 ;				// 0 degrees offset

	phase_offset = 45.0 * 3.14159 / 180.0 ;

	cout << "Fgain = " << fgain << endl;

	//	Initialize the nyquist delay lines, the EVM and SNR measurement
	snr = 0.0 ;

/////////////////////////
// START OF THE MAIN LOOP
/////////////////////////
	fphex << "Boost\n";

	for ( i = 0 ; i < NUM_SYMBOLS ; i++ ) {		//Number of symbols per update
		//	progress indicator.
		if (i % 100000 == 0)
			cout << "program " << i << "/" << NUM_SYMBOLS << " completed." << endl;

		//	Randomly generate 1 of 4 possible QPSK symbols ;
		if ( i % 256 == 0 ) {
			wci_get_qpsk (	&i_symb,				// Next symbol's I value
							&q_symb ) ;				// Next symbol's Q value
			cPwr += (i_symb * i_symb) + (q_symb * q_symb);
		}
		else {
			i_symb = 0 ;
			q_symb = 0 ;
		}

		//	Nyquist filter the symbol
		wci_nyq_filt_boost (  i_symb,					// I symbol input
						q_symb,					// Q symbol input
						nyq_coeffs,				// Nyquist coefficients for I and Q
						nyq_delay_i,			// I delay line storage
						nyq_delay_q,			// Q delay line storage
						&nyq_iout,	 			// I output from Nyquist filter
						&nyq_qout);				// Q output from Nyquist filter

//	Add gaussian noise and measure the SNR
		i_symb_noise = (double)nyq_iout / (double)fgain ;
		q_symb_noise = (double)nyq_qout / (double)fgain ;

		isymb_temp = i_symb_noise * cos(phase_offset) - q_symb_noise * sin(phase_offset) ;
		qsymb_temp = i_symb_noise * sin(phase_offset) + q_symb_noise * cos(phase_offset) ;

		i_symb_noise = isymb_temp ;
		q_symb_noise = qsymb_temp ;

		wci_get_gauss ( &i_noise ) ;
		wci_get_gauss ( &q_noise ) ;

		i_symb_noise += (i_noise * snr_scale) ;
		q_symb_noise += (q_noise * snr_scale) ;

		nPwr += (i_noise * snr_scale) * (i_noise * snr_scale) +
				(q_noise * snr_scale) * (q_noise * snr_scale);

		//	Add the DC Offsets to simulate Mixer
		i_symb_noise = i_symb_noise + i_dc_offset ;
		q_symb_noise = q_symb_noise + q_dc_offset ;

		//	Simulate the Phase Imbalance
		yi_cos_theta = i_symb_noise * cos(theta) ;
		yi_sin_theta = i_symb_noise * sin(theta) ;
		yq_cos_theta = q_symb_noise * cos(theta) ;
		yq_sin_theta = q_symb_noise * sin(theta) ;

		yi_sum_phase = yi_cos_theta + yq_sin_theta ;
		yq_sum_phase = yi_sin_theta + yq_cos_theta ;

		//	Simulate the Gain Imbalance
		zi = yi_sum_phase * (1.0 + gain_imbalance) ;
		zq = yq_sum_phase * (1.0 - gain_imbalance) ;

		wci_nyq_filt_boost (  (intSigGen)zi,			// I symbol BB output
						(intSigGen)zq,			// Q symbol BB output
						res_coeffs,				// Nyquist coefficients for I and Q
						res_delay_i,			// I delay line storage
						res_delay_q,			// Q delay line storage
						&res_iout,	 			// I output from Nyquist filter
						&res_qout);				// Q output from Nyquist filter


		res_iout = (intSigGen)(((double)res_iout)/(double)(2048.0)) ;
		res_qout = (intSigGen)(((double)res_qout)/(double)(2048.0)) ;

		fphex << setfill ('0') << setw(8) << hex;
		fphex << (16*res_iout) << endl;
		fphex << setfill ('0') << setw(8) << hex;
		fphex << (16*res_qout) << endl;

	}

	nPwr += 1.0E-12;
	snr = 10 * log10(cPwr * 64.0 / nPwr);
	cout << "snr is " << snr << endl;

	cout << "end of sim" << endl;
	stopTime = time(NULL);
	elapsedTime = stopTime - startTime;
	cout << "runtime: " << elapsedTime << endl;

	return 0 ;
}
///////////////////////
// END OF THE MAIN LOOP
///////////////////////

void wci_get_qpsk ( intSigGen *i_symb, intSigGen *q_symb ) {

	int64_t	next ;

	next = rand ();
	if ( next < RAND_MAX/2 )
		*i_symb = +64 ;
	else
		*i_symb = -64 ;

	next = rand () ;
	if ( next < RAND_MAX/2 )
		*q_symb = +64 ;
	else
		*q_symb = -64 ;
}	// End of wci_get_qpsk

void wci_get_gauss ( double *noise) {

	double	gauss_num ;
	intSigGen	num_avg ;
	intSigGen	i ;

	num_avg = 10 ;
	gauss_num = 0.0 ;

	for (i = 0; i < num_avg; i++) {
		gauss_num += ((double)rand()/(double)RAND_MAX - 0.5);
	}

	*noise = gauss_num * sqrt((double)12/(double)num_avg) ;

}	// End of wci_get_gauss

void wci_nyq_filt ( intSigGen i_symb,				// I symbol input
					intSigGen q_symb,				// Q symbol input
					vector<intSigGen> &nyq_coeffs,		// Nyquist coefficients for I and Q
					vector<intSigGen> &nyq_delay_i,		// I delay line storage
					vector<intSigGen> &nyq_delay_q,		// Q delay line storage
					intSigGen *nyq_iout,	 		// I output from Nyquist filter
					intSigGen *nyq_qout) 			// Q output from Nyquist filter
{
	vector<intSigGen>::iterator	iter;
	vector<intSigGen>::iterator	iterCoeffs;
	vector<intSigGen>::iterator	iterDelayI;
	vector<intSigGen>::iterator	iterDelayQ;

//	Compute the next filter output
	*nyq_iout = inner_product(nyq_coeffs.begin(),
							  nyq_coeffs.end(),
							  nyq_delay_i.begin(),
							  0);
	*nyq_qout = inner_product(nyq_coeffs.begin(),
							  nyq_coeffs.end(),
							  nyq_delay_q.begin(),
							  0);

//	Shift the delay lines and put the next symbol in the delay lines
	nyq_delay_i.pop_back();
	iter = nyq_delay_i.begin();
	nyq_delay_i.insert(iter, i_symb);

	nyq_delay_q.pop_back();
	iter = nyq_delay_q.begin();
	nyq_delay_q.insert(iter, q_symb);
}

//	Routine to get the 9 coefficients for a specified delay index (0-127)
void get_rs_coeffs (intSigGen *p,					// Pointer to the first coefficient
					intSigGen  resamp_index,		// Delay index
					vector<intSigGen> &res_coeffs	// Array of current 9 resampler coefficients
					)
{
	intSigGen	i ;
	intSigGen	*q ;

	q = p + (intSigGen)(resamp_index * 9) ;

	for( i = 0 ; i < 9 ; i++ ) {
		res_coeffs.at(i) = (intSigGen)*q ;
		q = q + 1 ;
	}
}

void wci_nyq_filt_boost ( intSigGen i_symb,				// I symbol input
		intSigGen q_symb,				// Q symbol input
		vector<intSigGen> &nyq_coeffs,	// Nyquist coefficients for I and Q
		boost::circular_buffer<intSigGen> &nyq_delay_i,		// I delay line storage
		boost::circular_buffer<intSigGen> &nyq_delay_q,		// Q delay line storage
		intSigGen *nyq_iout,	 		// I output from Nyquist filter
		intSigGen *nyq_qout)			// Q output from Nyquist filter
{
	int32_t sumI, sumQ;
	sumI = sumQ = 0;


	int32_t 
		*arr_i = nyq_delay_i.array_one().first,
		*arr_q = nyq_delay_q.array_one().first;
	size_t size = nyq_delay_i.array_one().second;

	//Compute the next filter output
	size_t K;
	for (K=0 ; K < size ; K++ ) {
		sumI += arr_i[K] * nyq_coeffs[K];
		sumQ += arr_q[K] * nyq_coeffs[K];
	}
	arr_i = nyq_delay_i.array_two().first;
	arr_q = nyq_delay_q.array_two().first;
	size = nyq_delay_i.array_two().second;

	for (size_t L=0; L < size; L++,K++ ) {
		sumI += arr_i[L] * nyq_coeffs[K];
		sumQ += arr_q[L] * nyq_coeffs[K];
	}

	*nyq_iout = sumI;
	*nyq_qout = sumQ;

	nyq_delay_i.push_front(i_symb);
	nyq_delay_q.push_front(q_symb);
}

bool load_from_file(const string &filename, std::vector<intSigGen> &coeff){ //coeff must have the correct size
	ifstream file(filename.c_str());
	double aux;
	size_t K=0;
	for(; K<coeff.size() and file>>aux; ++K)
		coeff[K] = aux;
	return K==coeff.size();
}
