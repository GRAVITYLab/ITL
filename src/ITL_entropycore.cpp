#include "ITL_entropycore.h"

float ITL_entropycore::computeEntropy_HistogramBased( int* binIds, int nPoint, int nBin, bool toNormalize )
{
	// Initialize probability array
	float* probArray = new float[nBin];
	for( int i=0; i<nBin; i++ )
		probArray[i] = 0;

	// Scan through bin Ids and keep count
	for( int i=0; i<nPoint; i++ )
		probArray[ binIds[i] ] ++;

	// Turn count into probabilities
	for( int i=0; i<nBin; i++ )
		probArray[i] /= (float)nPoint;

	// Compute negative entropy
	float entropy = 0;
	for( int i = 0; i<nBin; i++ )
	{
		entropy += ( probArray[i] * ( probArray[i] == 0 ? 0 : ( log( probArray[i] ) / log(2.0) ) ) );
	}

	// Change sign
	entropy = -entropy;
	
	// Normalize, if required
	if( toNormalize )
		entropy /= ( log( nBin ) / log( 2.0f ) );

	// Free temporary resources
	delete [] probArray;

	return entropy;
	
}// End function

float ITL_entropycore::computeEntropy_KDEBased( float* data, int nPoint, float h, bool toNormalize )
{
	// Compute mean and variance of data
	float mu = ITL_statutil<float>::Mean( data, nPoint );
	float var = ITL_statutil<float>::Variance( data, nPoint, mu );
	printf( "Mean and variance of the field: %f %f\n", mu, var );
	
	// Initialize probability array
	float* probArray = new float[nPoint];
	for( int i=0; i<nPoint; i++ )
		probArray[i] = 0;
	
	// Estimale kernel bandwidth
	if( h == 0 )
		h = 1.06 * var * pow( nPoint,-0.2f ) ;
        printf( "Number of points in the field: %d\n", nPoint );	
	printf( "Kernel bandwidth: %f\n", h );

	// Estimate probabilities at each point
	float sumOfKernels = 0;
	for( int i=0; i<nPoint; i++ )
	{
		sumOfKernels = 0;
		for( int j=0; j<nPoint; j++ )
		{
			sumOfKernels += ITL_entropycore::evaluateKernel( ( data[i] - data[j] ) / h, mu, var ); 
		}
		probArray[i] = sumOfKernels / (nPoint*h);
	}
	
	// Compute negative entropy
	float entropy = 0;
	for( int i = 0; i<nPoint; i++ )
		entropy += ( probArray[i] * ( probArray[i] == 0 ? 0 : ( log( probArray[i] ) / log(2.0) ) ) );
	
	// Change sign
	entropy = -entropy;
	
	// Normalize, if required
	if( toNormalize )
		entropy /= ( log( nPoint ) / log( 2.0f ) );

	// Free temporary resources
	delete [] probArray;

	return entropy;
	
}// End function

float ITL_entropycore::evaluateKernel( float x, float mu, float var )
{
	float exponent = - ( x - mu )/ (2*var);
	return ( 1.0f / sqrt( 2*pi*var ) ) * exp( exponent );	
	
}// End function