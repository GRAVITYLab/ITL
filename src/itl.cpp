//---------------------------------------------------------------------------
//
// itl wrappers, callable from C, C++, and Fortran
//
// Tom Peterka
// Argonne National Laboratory
// 9700 S. Cass Ave.
// Argonne, IL 60439
// tpeterka@mcs.anl.gov
//
// Copyright Notice
// + 2010 University of Chicago
//
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// preprocessors

	//! The maximal dimension of blocks. 4 should be sufficient for most scientific data
	#define MAX_BLOCK_DIM	4

//--------------------------------------------------------------------------
// ITL-relarted headers
	#include "ITL_header.h"
	#include "ITL_base.h"
	#include "ITL_ioutil.h"
	#include "ITL_vectormatrix.h"
	#include "ITL_localentropy.h"
	#include "ITL_globalentropy.h"
	#include "ITL_localjointentropy.h"
	#include "ITL_field_regular.h"

	// header for the wrapper
	#include "itl.h"

	// headers from my own libraries (mylib)
	#include "liblog.h"
	#include "libbuf3d.h"

	//! The rank of the current process
	static int iRank;

#if	0	// TEST-MOD
		//! the order of the array
		/*!
		 * Currently only row major order (0) is supported.
		 */
		static int iOrder;

		//! The flag whether the vector orientation is used when building histogram
		static bool bIsUsingVectorOrientation;

		//! The flag whether the local entropy is computed
		static bool bIsComputingLocalEntropy;

		//! The flag whether the geometry is dumped
		static bool bIsDumpingGeometry;

		//! #bins in the histogram
		static int iNrOfBins = 360;

		//! a flag whether the computed entropy is dumped
		static bool bIsDumpingEntropies;

		//! a flag whether the computed random samples are dumped
		static bool bIsDumpingSamples;

		//! a flag whether the computation is applied to all blocks. (false for block-wise computation)
		static bool bIsExecutingAllBlocks;

		//! a buffer to store block-wised entropy of all block
		static TBuffer<float> pfBlockEntropies;

		//! # of blocks.
		static int iNrOfBlocks = 0;

		//! ID (0-based) of te bound block
		static int iBoundBlockId = -1;

		//! the size of a local neighborhood
		static int piNeighborhood[MAX_BLOCK_DIM] = {6, 6, 6, 0};

		static int piLowPad[MAX_BLOCK_DIM] 	= {0, 0, 0, 0};
		static int piHighPad[MAX_BLOCK_DIM] = {0, 0, 0, 0};
		static float pfBlockDimLow[MAX_BLOCK_DIM]  	= {0.0f, 0.0f, 0.0f, 0.0f};
		static float pfBlockDimUp[MAX_BLOCK_DIM]  	= {0.0f, 0.0f, 0.0f, 0.0f};

		//! Length of each dim. For unused dimension, the value should be 1.
		static int piBlockDimLengths[MAX_BLOCK_DIM];

		//! structure for accessing elements passed from the application
		typedef struct CArray
		{
			//! pointer to the beginning of the pool passed by the application
			double *pdData;

			//! offset (0-based) of the first element in this pool
			int iBase;

			//! step between consecutive elements
			int iStep;
		} CArray;

		static TBuffer<CArray> pcFeatures;

		//! a union to store the geometry of a block
		typedef union CGeometry
		{
			struct CRect {
				CArray pcAxes[MAX_BLOCK_DIM];
			} cRect;
		} CGeometry;
		CGeometry cGeometry;


	//--------------------------------------------------------------------------
	// functions

	//! The C and C++ API to initialize ITL
	/*!
	 *
	*/
	void
	ITL_begin()
	{
		// Get the rank of the current processors
		MPI_Comm_rank(MPI_COMM_WORLD, &::iRank);

		// Initialize ITL
		ITL_base::ITL_init();

		// Initialize histogram
		// "!" means the default patch file
		ITL_histogram::ITL_init_histogram( "!", iNrOfBins );

		// register ITL_end to be called at the end of excution (in case the application does not call it)
		atexit(ITL_end);
	}

	//! The Fortran API to initialize ITL
	/*!
	 * \sa ITL_begin
	*/
	extern "C"
	void
	itl_begin_()
	{
		ITL_begin();
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to free ITL
	/*!
	 *
	*/
	void
	ITL_end
	(
	)
	{
		// in order not to dump too many files, the dumpping of block-wise global entropy is
		// executed here. This is a temporary design and should be revised in the
		// future

		if( ::bIsDumpingEntropies && false == ::bIsComputingLocalEntropy)
		{
			// dump the vector field
			char szEntropyPathPrefix[1024];
			sprintf(szEntropyPathPrefix, "dump/ge.rank_%d", ::iRank);
			::pfBlockEntropies._Save(szEntropyPathPrefix);
		}
	}

	//! The Fortran API to free ITL
	/*!
	 * \sa ITL_end
	*/
	extern "C"
	void itl_end_
	(
	)
	{
		ITL_end();
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify #blocks
	/*!
	\param iNrOfBlocks		#blocks
	*/
	void
	ITL_nblocks
	(
		const int iNrOfBlocks
	)
	{
		ASSERT_OR_LOG(	// the #blocks must be large than 1.
			1 <= iNrOfBlocks,
			fprintf(stderr, "Invalid #blocks %d", iNrOfBlocks) );
		::iNrOfBlocks = iNrOfBlocks;

		pfBlockEntropies.alloc(iNrOfBlocks);
	}

	//! The Fortran API to specify #blocks and length of each element
	/*!
	 * \sa ITL_nblocks
	*/
	extern "C"
	void
	itl_nblocks_
	(
		int *piNrOfBlocks
	)
	{
		ITL_nblocks(*piNrOfBlocks);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify length of feature vector on each element
	/*!
	\param iFeatureLength 	length of each elements. Currently only 1, 2, and 3 are supported.
	*/
	void
	ITL_feature_length
	(
		const int iFeatureLength
	)
	{
		ASSERT_OR_LOG(	// the feature length must be large than 1
			1 <= iFeatureLength,
			fprintf(stderr, "Invalid feature length %d", iFeatureLength) );
		::pcFeatures.alloc(iFeatureLength);
	}

	//! The Fortran API to specify length of feature vector on each element
	/*!
	 * \sa ITL_feature_length
	*/
	extern "C"
	void
	itl_feature_length_
	(
		int *piFeatureLength
	)
	{
		ITL_feature_length(*piFeatureLength);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to bind the block.
	/*!
	\param iBlockId Id of the block (0-based)
	*/
	void
	ITL_bind_block
	(
		const int iBlockId
	)
	{
		::iBoundBlockId = iBlockId;
	}

	//! The Fortran API to bind the block.
	/*!
	\param piBlockId pointer to the block id (1-based)
	\sa ITL_bind_block
	*/
	extern "C"
	void
	itl_bind_block_
	(
		int *piBlockId
	)
	{
		ITL_bind_block(
			*piBlockId - 1	// convert the block Id from 1-based (Fortran) to C (0-based)
		);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify the block order
	/*!
	\param iOrder	Order of the data block.
					0: row-major order.
					1: column-major order
					2: no order (treat it as a 1D array)
					NOTE: currenly only row-major order is supported
	*/
	void
	ITL_block_order
	(
		const int iOrder
	)
	{
		ASSERT_OR_LOG(
			0 == iOrder,
			fprintf(stderr, "Currently only row-major order (Order = 0) is supported") );

		::iOrder = iOrder;
	}

	//! The Fortran API to specify the block order
	/*!
	\sa   ITL_block_order
	*/
	extern "C"
	void
	itl_block_order_
	(
		int *piOrder
	)
	{
		ITL_block_order
		(
			*piOrder
		);
	}


	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify the block size
	/*!
	\param iBlockDim 	Dimension of the block
	\param piBlockDimLengths
						The length of each dimension. The size should be
						equal to iBlockDim. If length is large than MAX_BLOCK_DIM,
						those elements exceed MAX_BLOCK_DIM will be ignored.
	\sa   MAX_BLOCK_DIM
	*/
	void
	ITL_block_size
	(
		const int iBlockDim,
		const int piBlockDimLengths[]
	)
	{
		for(int i = 0; i < MAX_BLOCK_DIM; i++)
		{
			int iDimLength = ( i < iBlockDim )?piBlockDimLengths[i]:1;
			ASSERT_OR_LOG(
					iDimLength>=1,
					fprintf(stderr, "Invalid length (%d) for dim %d", iDimLength, i) );
			::piBlockDimLengths[i] = iDimLength;

			::pfBlockDimLow[i] = 	0.0f;
			::pfBlockDimUp[i] = 	(float)iDimLength - 1.0f;
		}
	}

	//! The Fortran API to specify the block size
	/*!
	\sa   ITL_block_size
	*/
	extern "C"
	void
	itl_block_size_
	(
		int *piBlockDim,
		int *piBlockDimLengths
	)
	{
		ITL_block_size
		(
			*piBlockDim,
			piBlockDimLengths
		);
	}

	//! The Fortran API to specify the size for 2D block
	/*!
	\sa   ITL_block_size
	*/
	extern "C"
	void
	itl_block_size2_
	(
		int *piBlockXLength,
		int *piBlockYLength
	)
	{
		int piBlockDimLengths[2];
		piBlockDimLengths[0] = *piBlockXLength;
		piBlockDimLengths[1] = *piBlockYLength;

		ITL_block_size
		(
			2,
			piBlockDimLengths
		);
	}

	//! The Fortran API to specify the size for 3D block
	/*!
	\sa   ITL_block_size
	*/
	extern "C"
	void
	itl_block_size3_
	(
		int *piBlockXLength,
		int *piBlockYLength,
		int *piBlockZLength
	)
	{
		int piBlockDimLengths[3];
		piBlockDimLengths[0] = *piBlockXLength;
		piBlockDimLengths[1] = *piBlockYLength;
		piBlockDimLengths[2] = *piBlockZLength;

		ITL_block_size
		(
			3,
			piBlockDimLengths
		);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify the extent within the block.
	/*!
	\param iBlockDim 	Dimension of the block
	\param piBlockDimLow	Lower bound of each dimension
	\param piBlockDimUp		Upper bound of each dimension
						The extent of each dimension. The unit is in cells.
						The size should be equal to iBlockDim. If length is
						large than MAX_BLOCK_DIM, those elements exceed MAX_BLOCK_DIM will be ignored.

	\sa   MAX_BLOCK_DIM

	The C and C++ API to specify the extend within the block. The entropy will
	be only computed within that region.
	*/
	void
	ITL_block_extent_in_cells
	(
		const int iBlockDim,
		const int piBlockDimLow[],
		const int piBlockDimUp[]
	)
	{
		for(int i = 0; i < min(MAX_BLOCK_DIM, iBlockDim); i++)
		{
			::pfBlockDimLow[i] = 	(float)piBlockDimLow[i];
			::pfBlockDimUp[i] = 	(float)piBlockDimUp[i];
		}
	}

	//! The Fortran API to specify the extent within the block.
	/*!
	\sa   ITL_block_extent_in_cells
	*/
	extern "C"
	void
	itl_block_extent_in_cells_
	(
		int *piBlockDim,
		int *piBlockDimLow,
		int *piBlockDimUp
	)
	{
		ITL_block_extent_in_cells
		(
			*piBlockDim,
			piBlockDimLow,
			piBlockDimUp
		);
	}

	//! The Fortran API to specify the 2D extent within the block.
	/*!
	\sa   ITL_block_extent_in_cells
	*/
	extern "C"
	void
	itl_block_extent_in_cells2_
	(
		int *piBlockXLow,
		int *piBlockYLow,
		int *piBlockXUp,
		int *piBlockYUp
	)
	{
		int piBlockDimLow[2];
		piBlockDimLow[0] = *piBlockXLow;
		piBlockDimLow[1] = *piBlockYLow;

		int piBlockDimUp[2];
		piBlockDimUp[0] = *piBlockXUp;
		piBlockDimUp[1] = *piBlockYUp;

		ITL_block_extent_in_cells
		(
			2,
			piBlockDimLow,
			piBlockDimUp
		);
	}

	//! The Fortran API to specify the 3D extent within the block.
	/*!
	\sa   ITL_block_extent_in_cells
	*/
	extern "C"
	void
	itl_block_extent_in_cells3_
	(
		int *piBlockXLow,
		int *piBlockYLow,
		int *piBlockZLow,
		int *piBlockXUp,
		int *piBlockYUp,
		int *piBlockZUp
	)
	{
		int piBlockDimLow[2];
		piBlockDimLow[0] = *piBlockXLow;
		piBlockDimLow[1] = *piBlockYLow;
		piBlockDimLow[2] = *piBlockZLow;

		int piBlockDimUp[2];
		piBlockDimUp[0] = *piBlockXUp;
		piBlockDimUp[1] = *piBlockYUp;
		piBlockDimUp[2] = *piBlockZUp;

		ITL_block_extent_in_cells
		(
			3,
			piBlockDimLow,
			piBlockDimUp
		);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify the neighborhood size
	/*!
	\param iBlockDim 	Dimension of the block
	\param pdLocalNeighborhood
						The length of each dimension. The size should be
						equal to iBlockDim. If length is large than MAX_BLOCK_DIM,
						those elements exceed MAX_BLOCK_DIM will be ignored.
	\sa   MAX_BLOCK_DIM
	*/
	void
	ITL_local_neighborhood_in_cells
	(
		const int iBlockDim,
		const double pdLocalNeighborhood[]
	)
	{
		for(int i = 0; i < MAX_BLOCK_DIM; i++)
		{
			int iDimNeighborhood = ( i < iBlockDim )?(int)pdLocalNeighborhood[i]:0;
			ASSERT_OR_LOG(
					iDimNeighborhood >= 0,
					fprintf(stderr, "Invalid Neighborhood (%d) for dim %d", iDimNeighborhood, i) );
			::piNeighborhood[i] = iDimNeighborhood;
		}
	}

	//! The Fortran API to specify the neighborhood size
	/*!
	\sa   ITL_local_neighborhood_in_cells
	*/
	extern "C"
	void
	itl_local_neighborhood_in_cells_
	(
		int *piBlockDim,
		double *pdNeighborhood
	)
	{
		ITL_local_neighborhood_in_cells
		(
			*piBlockDim,
			pdNeighborhood
		);
	}

	//! The Fortran API to specify the 2D neighborhood size
	/*!
	\sa   ITL_local_neighborhood_in_cells
	*/
	extern "C"
	void
	itl_local_neighborhood2_
	(
		double *pdXNeighborhood,
		double *pdYNeighborhood
	)
	{
		double pdNeighborhood[2];
		pdNeighborhood[0] = *pdXNeighborhood;
		pdNeighborhood[1] = *pdYNeighborhood;
		ITL_local_neighborhood_in_cells
		(
			2,
			pdNeighborhood
		);
	}

	//! The Fortran API to specify the 3D neighborhood size
	/*!
	\sa   ITL_local_neighborhood_in_cells
	*/
	extern "C"
	void
	itl_local_neighborhood3_
	(
		double *pdXNeighborhood,
		double *pdYNeighborhood,
		double *pdZNeighborhood
	)
	{
		double pdNeighborhood[3];
		pdNeighborhood[0] = *pdXNeighborhood;
		pdNeighborhood[1] = *pdYNeighborhood;
		pdNeighborhood[2] = *pdZNeighborhood;
		ITL_local_neighborhood_in_cells
		(
			3,
			pdNeighborhood
		);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify the pointer, start, and step to access the data
	/*!
	\param	iFeatureIndex	Index (0-based) of the feature vector
	\param	pdData			The data pool
	\param	iBase 			Index (0-based) to the 1st element in the pool
	\param	iStep			Index difference between consecutive elements in the pool
	*/
	void
	ITL_feature_data
	(
		const int iFeatureIndex,
		double pdData[],
		const int iBase,
		const int iStep
	)
	{
		ASSERT_OR_LOG(
			0 <= iFeatureIndex && iFeatureIndex < (int)::pcFeatures.USize(),
			fprintf(stderr, "Out-of-range Feature Index") );

		::pcFeatures[iFeatureIndex].pdData = pdData;
		::pcFeatures[iFeatureIndex].iBase = iBase;
		::pcFeatures[iFeatureIndex].iStep = iStep;
	}

	//! The Fortran ++ API to specify the pointer, start, and step to access the data
	/*!
	\param	piFeatureIndex	Pointer to the index (1-based) of the feature vector
	\param	pdData			The data pool
	\param	iBase 			Pointer to the index (1-based) to the 1st element in the pool
	\param	iStep			Pointer to the index difference between consecutive elements in the pool

	\sa		ITL_feature_data
	*/
	extern "C"
	void
	itl_feature_data_
	(
		int *piFeatureIndex,
		double *pdData,
		int *piBase,
		int *piStep
	)
	{
		ITL_feature_data
		(
			*piFeatureIndex - 1,
			pdData,
			*piBase - 1,
			*piStep
		);
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify whether the vector orientation is used as the random variable
	/*!
	*/
	void
	ITL_is_using_vector_orientation
	(
		bool bIsEnabled
	)
	{
		::bIsUsingVectorOrientation = bIsEnabled;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \sa ITL_is_using_vector_orientation
	*/
	extern "C"
	void
	itl_is_using_vector_orientation_
	(
		int *piIsEnabled
	)
	{
		ITL_is_using_vector_orientation( (0 != *piIsEnabled)?true:false );
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify whether the entropy field is computed
	/*!
	*/
	void
	ITL_is_computing_local_entropy
	(
		bool bIsEnabled
	)
	{
		::bIsComputingLocalEntropy = bIsEnabled;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \sa ITL_is_computing_local_entropy
	*/
	extern "C"
	void
	itl_is_computing_local_entropy_
	(
		int *piIsComputingLocalEntropy
	)
	{
		ITL_is_computing_local_entropy( (0 != *piIsComputingLocalEntropy)?true:false );
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to trigger the computation of metrics
	/*!
	*/
	void
	ITL_execute
	(
	)
	{
		// currently, the time dimension is ignored
		ASSERT_OR_LOG(
				1 == piBlockDimLengths[3],
				fprintf(stderr, "4D blocks are not supported yet.") );

		// compute the #cells
		int iNrOfCells = 1;
		for(int d = 0;	d < MAX_BLOCK_DIM; d++)
		{
			iNrOfCells *= piBlockDimLengths[d];
		}

		if( ::bIsDumpingGeometry )
		{
			// for rectangular grid
			// Warning: currently the 4-th dimension is ignored

			#if	0	// TMP-MOD
					// dump the geometry per grid
					TBuffer<VECTOR3> pv3Geom;
					pv3Geom.alloc(iNrOfCells);

					// pack the 3 array into an array of vector3
					for(int i = 0, 	z = 0; z < ::piBlockDimLengths[2]; z++)
					{
						double *pdZCoord = ::cGeometry.cRect.pcAxes[2].pdData;
						int iZBase = ::cGeometry.cRect.pcAxes[2].iBase;
						int iZStep = ::cGeometry.cRect.pcAxes[2].iStep;
						float fZ = (float)pdZCoord[iZBase + z * iZStep];
						for(int 	y = 0; y < ::piBlockDimLengths[1]; y++)
						{
							double *pdYCoord = ::cGeometry.cRect.pcAxes[1].pdData;
							int iYBase = ::cGeometry.cRect.pcAxes[1].iBase;
							int iYStep = ::cGeometry.cRect.pcAxes[1].iStep;
							float fY = (float)pdYCoord[iYBase + y * iYStep];
							for(int x = 0; x < ::piBlockDimLengths[0]; x++, i++)
							{
								double *pdXCoord = ::cGeometry.cRect.pcAxes[0].pdData;
								int iXBase = ::cGeometry.cRect.pcAxes[0].iBase;
								int iXStep = ::cGeometry.cRect.pcAxes[0].iStep;
								float fX = (float)pdXCoord[iXBase + x * iXStep];
								pv3Geom[i] = VECTOR3(fX, fY, fZ);
							}
						}
					}

					char szGeomPathPrefix[1024];
					sprintf(szGeomPathPrefix, "dump/geom.rank_%d_blk_%d", ::iRank, iBoundBlockId);
					pv3Geom._Save(szGeomPathPrefix);
			#else
					// dump the geometry per axis
			char szGeomLog[1024];
			sprintf(szGeomLog, "dump/geom.rank_%d_blk_%d.txt", iRank, iBoundBlockId);
			FILE *fpGeomLog = fopen(szGeomLog, "wt");
			ASSERT_OR_LOG(NULL != fpGeomLog, perror(szGeomLog));
			fprintf(fpGeomLog, "%d\n", piBlockDimLengths[2]);

			double dMin;
			double dMax;
			double dPrev;

			dMin =+HUGE_VAL;
			dMax =-HUGE_VAL;
			dPrev = 0.0;
			for(int z = 0; z < ::piBlockDimLengths[2]; z++)
			{
				double *pdZCoord = ::cGeometry.cRect.pcAxes[2].pdData;
				int iZBase = ::cGeometry.cRect.pcAxes[2].iBase;
				int iZStep = ::cGeometry.cRect.pcAxes[2].iStep;
				double dC = pdZCoord[iZBase + z * iZStep];
				double dD = dC - dPrev;
				dPrev = dC;
				fprintf(fpGeomLog, "%e %e\n", dC, dD);
				dMin = min(dMin, dC);
				dMax = max(dMax, dC);
			}
			fprintf(fpGeomLog, "%e - %e\n", dMin, dMax);

			fprintf(fpGeomLog, "%d\n", piBlockDimLengths[1]);
			dMin =+HUGE_VAL;
			dMax =-HUGE_VAL;
			dPrev = 0.0;
			for(int y = 0; y < ::piBlockDimLengths[1]; y++)
			{
				double *pdYCoord = ::cGeometry.cRect.pcAxes[1].pdData;
				int iYBase = ::cGeometry.cRect.pcAxes[1].iBase;
				int iYStep = ::cGeometry.cRect.pcAxes[1].iStep;
				double dC = pdYCoord[iYBase + y * iYStep];
				double dD = dC - dPrev;
				dPrev = dC;
				fprintf(fpGeomLog, "%e %e\n", dC, dD);
				dMin = min(dMin, dC);
				dMax = max(dMax, dC);
			}
			fprintf(fpGeomLog, "%e - %e\n", dMin, dMax);

			fprintf(fpGeomLog, "%d\n", piBlockDimLengths[0]);
			dMin =+HUGE_VAL;
			dMax =-HUGE_VAL;
			dPrev = 0.0;
			for(int x = 0; x < ::piBlockDimLengths[0]; x++)
			{
				double *pdXCoord = ::cGeometry.cRect.pcAxes[0].pdData;
				int iXBase = ::cGeometry.cRect.pcAxes[0].iBase;
				int iXStep = ::cGeometry.cRect.pcAxes[0].iStep;
				double dC = pdXCoord[iXBase + x * iXStep];
				double dD = dC - dPrev;
				dPrev = dC;
				fprintf(fpGeomLog, "%e %e\n", dC, dD);
				dMin = min(dMin, dC);
				dMax = max(dMax, dC);
			}
			fprintf(fpGeomLog, "%e - %e\n", dMin, dMax);

			fclose(fpGeomLog);
			#endif
		}

		bool bIsUsingVectorsAsRandomVariables = false;
		TBuffer3D<VECTOR3> p3Dv3Vector;

		//! the buffer to store the generated random variable as scalar
		TBuffer3D<float> p3DfRandomSamples;

		if( ::bIsUsingVectorOrientation )
		{
			ASSERT_OR_LOG(
				2 == ::pcFeatures.USize() || 3 == ::pcFeatures.USize(),
				fprintf(stderr, "Vector orientation is only supported for 2D or 3D.") );

			switch(pcFeatures.USize())
			{
			case 3:
			{
				p3Dv3Vector.alloc(piBlockDimLengths[0], piBlockDimLengths[1], piBlockDimLengths[2]);
				for(int c = 0;	c < iNrOfCells; c++)
					for(int f = 0; f < (int)pcFeatures.USize(); f++)
					{
						double *pdData = pcFeatures[f].pdData;
						int iBase = pcFeatures[f].iBase;
						int iStep = pcFeatures[f].iStep;
						p3Dv3Vector[c][f] = (float)pdData[iBase + c * iStep];
					}

				bIsUsingVectorsAsRandomVariables = true;
			}	break;

			case 2:
			{
				// use the magnitude of the feature vector as the random variable
				p3DfRandomSamples.alloc(piBlockDimLengths[0], piBlockDimLengths[1], piBlockDimLengths[2]);
				for(int c = 0;	c < iNrOfCells; c++)
				{
					double pdVector[2];
					for(int f = 0; f < (int)pcFeatures.USize(); f++)
					{
						double *pdData = pcFeatures[f].pdData;
						int iBase = pcFeatures[f].iBase;
						int iStep = pcFeatures[f].iStep;
						pdVector[f] = pdData[iBase + c * iStep];
					}
					double dAngle = atan2(pdVector[1], pdVector[0]);
					p3DfRandomSamples[c] = (float)dAngle;
				}

			}	break;
			} // switch
		}
		else
		{
			// use the magnitude of the feature vector as the random variable
			p3DfRandomSamples.alloc(piBlockDimLengths[0], piBlockDimLengths[1], piBlockDimLengths[2]);
			for(int c = 0;	c < iNrOfCells; c++)
			{
				double dMagnitude = 0.0;
				for(int f = 0; f < (int)pcFeatures.USize(); f++)
				{
					double *pdData = pcFeatures[f].pdData;
					int iBase = pcFeatures[f].iBase;
					int iStep = pcFeatures[f].iStep;
					double dF = pdData[iBase + c * iStep];
					if( 1 == pcFeatures.USize() )
						dMagnitude = dF;
					else
						dMagnitude += dF * dF;
				}
				if( 1 < pcFeatures.USize() )
					dMagnitude = sqrt(dMagnitude);

				p3DfRandomSamples[c] = (float)dMagnitude;
			}
		}

		if( bIsUsingVectorsAsRandomVariables )
		{
			// create a vector field by passing the pointer to the data represented by p3dv3Data
			ITL_field_regular<VECTOR3> *vectorField = new ITL_field_regular<VECTOR3>(
				&p3Dv3Vector[0],
				3,
				::pfBlockDimLow,
				::pfBlockDimUp,
				::piLowPad,
				::piHighPad,
				::piNeighborhood );

			if( ::bIsDumpingSamples )
			{
				char szSamplePathPrefix[1024];
				sprintf(szSamplePathPrefix, "dump/rs.rank_%d_blk_%d", ::iRank, iBoundBlockId);
				p3Dv3Vector._Save(szSamplePathPrefix);
			}

			if(bIsComputingLocalEntropy)
			{
				ITL_localentropy<VECTOR3> *localEntropyComputerForVector = new ITL_localentropy<VECTOR3>( vectorField );

				// convert each vector into bin index
				localEntropyComputerForVector->computeHistogramBinField("vector", iNrOfBins);

				// compute the entropy
				localEntropyComputerForVector->computeEntropyOfField( iNrOfBins, false);

				if( ::bIsDumpingEntropies )
				{
					ITL_field_regular<float>* entropyField = localEntropyComputerForVector->getEntropyField();
					char szEntropyPathPrefix[1024];
					sprintf(szEntropyPathPrefix, "dump/le.rank_%d.blk_%d.f.b3d", ::iRank, ::iBoundBlockId);
					ITL_ioutil<float>::writeFieldBinarySerial(
							entropyField->getDataFull(),
							szEntropyPathPrefix,
							entropyField->grid->dim,
							3 );
				}

				delete localEntropyComputerForVector;
			}
			else
			{
				ITL_globalentropy<VECTOR3> *globalEntropyComputerForVector = new ITL_globalentropy<VECTOR3>( vectorField );

				// convert each vector into bin index
				globalEntropyComputerForVector->computeHistogramBinField("vector", iNrOfBins);

				// compute the entropy
				globalEntropyComputerForVector->computeGlobalEntropyOfField(iNrOfBins, false);

				// save the block-wise entropy
				::pfBlockEntropies[::iBoundBlockId] = globalEntropyComputerForVector->getGlobalEntropy();

				delete globalEntropyComputerForVector;
			}
			delete vectorField;
		}
		else
		{
			// create a vector field by passing the pointer to the data represented by p3dv3Data
			ITL_field_regular<SCALAR> *scalarField = new ITL_field_regular<SCALAR>(
				&p3DfRandomSamples[0],
				3,
				::pfBlockDimLow,
				::pfBlockDimUp,
				::piLowPad,
				::piHighPad,
				::piNeighborhood );

			if( ::bIsDumpingSamples )
			{
				char szSamplePathPrefix[1024];
				sprintf(szSamplePathPrefix, "dump/rs.rank_%d_blk_%d", ::iRank, iBoundBlockId);
				p3DfRandomSamples._Save(szSamplePathPrefix);
			}

			if(bIsComputingLocalEntropy)
			{
				ITL_localentropy<SCALAR> *localEntropyComputerForScalar = new ITL_localentropy<SCALAR>( scalarField );

				// convert each vector into bin index
				localEntropyComputerForScalar->computeHistogramBinField("scalar", iNrOfBins);

				// compute the entropy
				localEntropyComputerForScalar->computeEntropyOfField( iNrOfBins, false);

				if( ::bIsDumpingEntropies )
				{
					ITL_field_regular<float>* entropyField = localEntropyComputerForScalar->getEntropyField();
					char szEntropyPathPrefix[1024];
					sprintf(szEntropyPathPrefix, "dump/le.rank_%d_blk_%d.f.b3d", ::iRank, ::iBoundBlockId);
					ITL_ioutil<float>::writeFieldBinarySerial(
							entropyField->getDataFull(),
							szEntropyPathPrefix,
							entropyField->grid->dim,
							3 );
				}

				delete localEntropyComputerForScalar;
			}
			else
			{
				ITL_globalentropy<SCALAR> *globalEntropyComputerForScalar = new ITL_globalentropy<SCALAR>( scalarField );

				// convert each vector into bin index
				globalEntropyComputerForScalar->computeHistogramBinField("scalar", iNrOfBins);

				// compute the entropy
				globalEntropyComputerForScalar->computeGlobalEntropyOfField(iNrOfBins, false);

				// save the block-wise entropy
				::pfBlockEntropies[::iBoundBlockId] = globalEntropyComputerForScalar->getGlobalEntropy();

				delete globalEntropyComputerForScalar;
			}
			delete scalarField;
		}
	}

	//! The Fortran API to trigger the computation of metrics
	/*!
	 * \sa ITL_execute
	*/
	extern "C"
	void
	itl_execute_
	(
	)
	{
		ITL_execute();
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify whether the geoometry is dumped
	/*!
	*/
	void
	ITL_is_dumping_geometry
	(
		bool bIsEnabled
	)
	{
		::bIsDumpingGeometry = bIsEnabled;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \sa ITL_is_dumping_geometry
	*/
	extern "C"
	void
	itl_is_dumping_geometry_
	(
		int *piIsEnabled
	)
	{
		ITL_is_dumping_geometry( (0 != *piIsEnabled)?true:false );
	}


	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify whether the random variable is dumped
	/*!
	*/
	void
	ITL_is_dumping_samples
	(
		bool bIsEnabled
	)
	{
		::bIsDumpingSamples = bIsEnabled;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \sa ITL_is_dumping_samples
	*/
	extern "C"
	void
	itl_is_dumping_samples_
	(
		int *piIsEnabled
	)
	{
		ITL_is_dumping_samples( (0 != *piIsEnabled)?true:false );
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify whether the entropy is dumped
	/*!
	*/
	void
	ITL_is_dumping_entropy
	(
		bool bIsEnabled
	)
	{
		::bIsDumpingEntropies = bIsEnabled;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \sa ITL_is_dumping_entropy
	*/
	extern "C"
	void
	itl_is_dumping_entropy_
	(
		int *piIsEnabled
	)
	{
		ITL_is_dumping_entropy( (0 != *piIsEnabled)?true:false );
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify whether the computation is applied to the entire domain.
	/*!
	 * If true, when call ITL_Execute, it will check where all blocks has been specified.
	 * If not, an error message will be thrown.
	 * Otherwise, ITL_Execute() will only compute the entropy within the current bound block.
	*/
	void
	ITL_is_executing_all_blocks
	(
		bool bIsEnabled
	)
	{
		ASSERT_OR_LOG(
			false == bIsEnabled,
			fprintf(stderr, "Currntly only block-wise computation is supported for in-situ.") );
		::bIsExecutingAllBlocks = bIsEnabled;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \sa ITL_is_executing_all_blocks
	*/
	extern "C"
	void
	itl_is_executing_all_blocks_
	(
		int *piIsEnabled
	)
	{
		ITL_is_executing_all_blocks( (0 != *piIsEnabled)?true:false );
	}

	/////////////////////////////////////////////////////////////////////
	//! The C and C++ API to specify the coordinates along one dim. for regular grid
	/*!
	 * \param iDimId	ID (0-based) of the block dimension
	 * \param pdCoord	the pool of the coordinates along the specified block dim
	 * \param iBase		the 1st element (0-based) in the pool
	 * \param iStep		the difference between the consecutive elements
	*/
	void
	ITL_geom_rect_dim_coord
	(
		const int iDimId,
		double *pdCoord,
		const int iBase,
		const int iStep
	)
	{
		ASSERT_OR_LOG(
			0 <= iDimId && iDimId < MAX_BLOCK_DIM,
			fprintf(stderr, "Invalid iDimId %d.", iDimId) );

		::cGeometry.cRect.pcAxes[iDimId].pdData = pdCoord;
		::cGeometry.cRect.pcAxes[iDimId].iBase = iBase;
		::cGeometry.cRect.pcAxes[iDimId].iStep = iStep;
	}

	//! The Fortran API to specify whether the vector orientation is used as the random variable
	/*!
	 * \param iDimId	ID (1-based) of the block dimension
	 * \param pdCoord	the pool of the coordinates along the specified block dim
	 * \param iBase		the 1st element (1-based) in the pool
	 * \param iStep		the difference between the consecutive elements
	 *
	 * \sa	ITL_geom_rect_dim_coord
	*/
	extern "C"
	void
	itl_geom_rect_dim_coord_
	(
		int *piDimId,
		double *pdCoord,
		int *piBase,
		int *piStep
	)
	{
		ITL_geom_rect_dim_coord
		(
			*piDimId - 1,	// from 1-based to 0-based index
			pdCoord,
			*piBase - 1,	// from 1-based to 0-based index
			*piStep
		);
	}
#else
	#include "ITL_random_field.h"

	char szGlobalEntropyLogPathFilename[1024];

	const int iNrOfBins = 360;

	ITLRandomField *pcBoundRandomField;

//--------------------------------------------------------------------------
// functions

//! The C/C++ API to initialize ITL
/*!
 *
*/
void
ITL_begin()
{
	// Get the rank of the current processors
	MPI_Comm_rank(MPI_COMM_WORLD, &::iRank);

	// Initialize ITL
	ITL_base::ITL_init();

	// Initialize histogram
	// "!" means the default patch file
	ITL_histogram::ITL_init_histogram( "!", iNrOfBins );

	// create a folder to hold the tmp. dumpped result
	if( 0 == iRank )
	{
		system("mkdir dump");
		system("rm dump/*");
	}
	MPI_Barrier(MPI_COMM_WORLD);

	// create the path/filename of the log for global entropy
	sprintf(szGlobalEntropyLogPathFilename, "dump/ge.rank_%d.log", ::iRank);

	// register ITL_end to be called at the end of execution (in case the application does not call it)
	atexit(ITL_end);
}

//! The Fortran API to initialize ITL
/*!
 * \sa ITL_begin
*/
extern "C"
void
itl_begin_()
{
	ITL_begin();
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to terminate ITL
/*!
 *
*/
void
ITL_end
(
)
{
}

//! The Fortran API to free ITL
/*!
 * \sa ITL_end
*/
extern "C"
void itl_end_
(
)
{
	ITL_end();
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to add a random field
/*!
\param	iNrOfBlocks				#blocks of the current process
\param	iNrOfDataComponents		#data components to be used for entropy computation
\param	piRfId					Pointer to the ID (0-based) of the created random field
*/
void
ITL_add_random_field
(
	const int iNrOfBlocks,
	const int iNrOfDataComponents,
	int *piRfId
)
{
	pcBoundRandomField = new ITLRandomField;
	pcBoundRandomField->_Create(iNrOfBlocks, iNrOfDataComponents);
	*piRfId = 0;
}

//! The Fortran API to free ITL
/*!
 * \sa ITL_add_random_field
*/
extern "C"
void
itl_add_random_field_
(
	int *piNrOfBlocks,
	int *piNrOfDataComponents,
	int *piRfId
)
{
	int iRfId;
	ITL_add_random_field
	(
		*piNrOfBlocks,
		*piNrOfDataComponents,
		&iRfId
	);
	*piRfId = iRfId + 1;
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to bind a random field
/*!
\param	iRfId	ID (0-based) of the random field
*/
void
ITL_bind_random_field
(
	const int iRfId
)
{
}

//! The Fortran API to bind a random field
/*!
\param	piRfId	Pointer to the ID (1-based) of the random field
 * \sa ITL_bind_random_field
*/
extern "C"
void
itl_bind_random_field_
(
	int *piRfId
)
{
	ITL_bind_random_field
	(
		*piRfId - 1	// 1-based to 0-based
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to bind a block.
/*!
\param iBlockId 	ID (0-based) of the block
*/
void
ITL_bind_block
(
	const int iBlockId
)
{
	pcBoundRandomField->_BindBlock(iBlockId);
}

//! The Fortran API to bind the block.
/*!
\param piBlockId 	Pointer to the ID (1-based) of the block
\sa ITL_bind_block
*/
extern "C"
void
itl_bind_block_
(
	int *piBlockId
)
{
	ITL_bind_block(
		*piBlockId - 1	// convert the block Id from 1-based (Fortran) to C (0-based)
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to specify the block size
/*!
\param iDim 		Dimension of the block
\param pikDimLengths
					The length of each dimension. The size should be
					equal to iDim. If length is large than CBlock::MAX_DIM,
					those elements exceed CBlock::MAX_DIM will be ignored.
\sa   CBlock::MAX_DIM
*/
void
ITL_block_size
(
	const int iDim,
	const int piDimLengths[]
)
{
	pcBoundRandomField->CGetBoundBlock()._SetBlockSize(iDim, piDimLengths);
}

//! The Fortran API to specify the block size
/*!
\sa   ITL_block_size
*/
extern "C"
void
itl_block_size_
(
	int *piDim,
	int *piDimLengths
)
{
	ITL_block_size
	(
		*piDim,
		piDimLengths
	);
}

//! The Fortran API to specify the size for 2D block
/*!
\sa   ITL_block_size
*/
extern "C"
void
itl_block_size2_
(
	int *piXLength,
	int *piYLength
)
{
	int piDimLengths[2];
	piDimLengths[0] = *piXLength;
	piDimLengths[1] = *piYLength;

	ITL_block_size
	(
		2,
		piDimLengths
	);
}

//! The Fortran API to specify the size for 3D block
/*!
\sa   ITL_block_size
*/
extern "C"
void
itl_block_size3_
(
	int *piXLength,
	int *piYLength,
	int *piZLength
)
{
	int piDimLengths[3];
	piDimLengths[0] = *piXLength;
	piDimLengths[1] = *piYLength;
	piDimLengths[2] = *piZLength;

	ITL_block_size
	(
		3,
		piDimLengths
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to bind a data component
/*!
\param iBlockId 	ID (0-based) of the data component
*/
void
ITL_bind_data_component
(
	const int iDataComponent
)
{
	pcBoundRandomField->_BindDataComponent(iDataComponent);
}

//! The Fortran API to bind the block.
/*!
\param piBlockId 	Pointer to the ID (1-based) of the data component
\sa ITL_bind_data_component
*/
extern "C"
void
itl_bind_data_component_
(
	int *piDataComponent
)
{
	ITL_bind_data_component(
		*piDataComponent - 1	// convert the Id from 1-based (Fortran) to C (0-based)
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to specify the range of the bound data component
/*!
\param dMin
\param dMax
*/
void
ITL_data_range
(
	const double dMin,
	const double dMax
)
{
	pcBoundRandomField->CGetBoundDataComponent().cRange._Set(dMin, dMax);
}

//! The Fortran API to specify the range of the bound data component
/*!
\sa ITL_data_range
*/
extern "C"
void
itl_data_range_
(
	double *pdMin,
	double *pdMax
)
{
	ITL_data_range
	(
		*pdMin,
		*pdMax
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to specify the data source of the bound data component
/*!
\param	pdData		The array of the data
\param	iBase		The ID (0-based) of the 1st element
\param	iStep		The distance between consecutive elements
*/
void
ITL_data_source
(
	const double pdData[],
	const int iBase,
	const int iStep
)
{
	pcBoundRandomField->_SetBoundArray
	(
		pdData,
		iBase,
		iStep
	);
}

//! The Fortran API to specify the data source of the bound data component
/*!
\param	piBase		Pointer to the ID (1-based) of the 1st element
\sa ITL_data_source
*/
extern "C"
void
itl_data_source_
(
	double *pdData,
	int *piBase,
	int *piStep
)
{
	ITL_data_source
	(
		pdData,
		*piBase - 1,	// 1-based to 0-based
		*piStep
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to specify the coordinates along one dim. for regular grid
/*!
 * \param iDimId	ID (0-based) of the block dimension
 * \param pdCoord	the array of the coordinates along the specified block dim
 * \param iBase		the 1st element (0-based) in the pool
 * \param iStep		the difference between the consecutive elements
*/
void
ITL_geom_rect_dim_coord
(
	const int iDimId,
	double *pdCoord,
	const int iBase,
	const int iStep
)
{
	::pcBoundRandomField->CGetBoundBlock().cGeometry._SetDimCoords(iDimId, pdCoord, iBase, iStep);
}

//! The Fortran API to specify whether the vector orientation is used as the random variable
/*!
 * \param *piDimId	Pointer to the ID (1-based) of the block dimension
 * \param *piBase	Pointer to the 1st element (1-based) in the array
 *
 * \sa	ITL_geom_rect_dim_coord
*/
extern "C"
void
itl_geom_rect_dim_coord_
(
	int *piDimId,
	double *pdCoord,
	int *piBase,
	int *piStep
)
{
	ITL_geom_rect_dim_coord
	(
		*piDimId - 1,	// from 1-based to 0-based index
		pdCoord,
		*piBase - 1,	// from 1-based to 0-based index
		*piStep
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to add a random variable
/*!
 * \param	piRvId		ID (0-based) of the new created random variable
*/
void
ITL_add_random_variable
(
	int *piRvId
)
{
	pcBoundRandomField->_AddRandomVariable(piRvId);
}

//! The Fortran API to add a random variable
/*!
 * \param	piRvId		Pointer to the ID (1-based) of the new created random variable
\sa ITL_add_random_variable
*/
extern "C"
void
itl_add_random_variable_
(
	int *piRvId
)
{
	int iRvId;
	ITL_add_random_variable
	(
		&iRvId
	);
	*piRvId = iRvId + 1;	// from 0-based to 1-based
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to bind a random variable
/*!
 * \param	iRvId		ID (0-based) of the random variable
*/
void
ITL_bind_random_variable
(
	const int iRvId
)
{
	pcBoundRandomField->_BindRandomVariable(iRvId);
}

//! The Fortran API to add a random variable
/*!
 * \param	piRvId		Pointer to the ID (1-based) of the random variable
 * \sa 		ITL_bind_random_variable
*/
extern "C"
void
itl_bind_random_variable_
(
	int *piRvId
)
{
	ITL_bind_random_variable
	(
		*piRvId - 1	// from 1-based to 0-based
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to set up the feature vector of the current random variable
/*!
\param	iFeatureLength			Length of the feature vector
\param	piFeatureVector			The array of indices (0-based) to the data components
\param	bIsUsingOrientation		A flag whether the vector orientation is used as the random variable (true) or the magnitude (false)
*/
void
ITL_random_varable_set_feature_vector
(
	const int iFeatureLength,
	const int piFeatureVector[],
	const bool bIsUsingOrientation
)
{
	pcBoundRandomField->_SetFeatureVector
	(
		iFeatureLength,
		piFeatureVector,
		bIsUsingOrientation
	);
}

//! The Fortran API to set up the feature vector of the current random variable
/*!
\param	piFeatureVector			The array of indices (1-based) to the data components
\sa 	ITL_random_varable_set_feature_vector
*/
extern "C"
void
itl_random_varable_set_feature_vector_
(
	int *piFeatureLength,
	int *piFeatureVector,
	int *piIsUsingOrientation
)
{
	// convert the indices from 1-based to 0-based
	int iFeatureLength = *piFeatureLength;
	TBuffer<int> piFeatureVector_0based;
	piFeatureVector_0based.alloc(iFeatureLength);
	for(int f = 0; f < iFeatureLength; f++)
		piFeatureVector_0based[f] = piFeatureVector[f] - 1;

	ITL_random_varable_set_feature_vector
	(
		iFeatureLength,
		&piFeatureVector_0based[0],
		(*piIsUsingOrientation)?true:false
	);
}

//! The Fortran API to set up a scalar as the feature vector of the current random variable
/*!
 * \param	piScalar	Pointer to the ID (1-based) of the data component
 * \sa 		ITL_random_varable_set_feature_vector
*/
extern "C"
void
itl_random_varable_as_scalar_
(
	int *piScalar
)
{
	int piFeatureVector[1];
	piFeatureVector[0] = *piScalar - 1;		// from 1-based to 0-based
	ITL_random_varable_set_feature_vector
	(
		sizeof(piFeatureVector)/sizeof(piFeatureVector[0]),
		piFeatureVector,
		false
	);
}

//! The Fortran API to set up a 3D vector as the feature vector of the current random variable
/*!
 * \param	piU		Pointer to the ID (1-based) of U vector component
 * \param	piV		Pointer to the ID (1-based) of V vector component
 * \param	piW		Pointer to the ID (1-based) of W vector component
 * \sa 		ITL_random_varable_set_feature_vector
*/
extern "C"
void
itl_random_varable_as_vector3_
(
	int *piU,
	int *piV,
	int *piW,
	int *piIsUsingOrientation
)
{
	int piFeatureVector[3];
	piFeatureVector[0] = *piU - 1;	// from 1-based to 0-based
	piFeatureVector[1] = *piV - 1;	// from 1-based to 0-based
	piFeatureVector[2] = *piW - 1;	// from 1-based to 0-based
	ITL_random_varable_set_feature_vector
	(
		sizeof(piFeatureVector)/sizeof(piFeatureVector[0]),
		piFeatureVector,
		(*piIsUsingOrientation)?true:false
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to dump the geometry of the bound block to a file
/*!
\param	szGeomPathFilename path/filename of the file
*/
void
ITL_dump_bound_block_geom
(
	const char* szGeomPathFilename
)
{
	::pcBoundRandomField->CGetBoundBlock()._DumpGeometry(szGeomPathFilename);
}

//! The Fortran API to dump the geometry of the bound block to a default file
/*!
*/
extern "C"
void
itl_dump_bound_block_geom_2tmp_
(
)
{
	char szGeomPathFilename[1024];
	sprintf(szGeomPathFilename, "dump/geom.rank_%d.block_%d.txt", ::iRank, ::pcBoundRandomField->IGetBoundBlock());
	ITL_dump_bound_block_geom(szGeomPathFilename);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to dump the feature vector of a specified random variable to a file
/*!
\param	iRvId	ID (0-based) of the random variable
\param	szFeatureVectorPathFilename	Path/filename of the file
*/
void
ITL_dump_bound_block_feature_vector_
(
	const int iRvId,
	const char* szFeatureVectorPathFilename
)
{
	::pcBoundRandomField->_DumpBoundBlockFeatureVector
	 (
		 iRvId,
		 szFeatureVectorPathFilename
	 );
}

//! The Fortran API to dump the feature vector of a specified random variable to a default file
/*!
\param	iRvId	ID (1-based) of the random variable
\sa		ITL_dump_bound_block_feature_vector
*/
extern "C"
void
itl_dump_bound_block_feature_vector_2tmp_
(
	int *piRvId
)
{
	const int iRvId = *piRvId - 1;
	char szFeatureVectorPathFilename[1024];
	sprintf(szFeatureVectorPathFilename, "dump/feature_vector.rank_%d.block_%d.rv_%d", ::iRank, ::pcBoundRandomField->IGetBoundBlock(), iRvId);
	ITL_dump_bound_block_feature_vector_
	(
		iRvId,
		szFeatureVectorPathFilename
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to compute and dump the global entropy of the bound block to a file
/*!
\param	iRvId							ID (0-based) of the random variable
\param	szGlobalEntropyLogPathFilename	Path/filename of the file
*/
void
ITL_dump_bound_block_global_entropy
(
	const int iRvId,
	const char* szGlobalEntropyLogPathFilename
)
{
	pcBoundRandomField->_ComputeEntorpyInBoundBlock
	(
		iRvId,
		 szGlobalEntropyLogPathFilename
	);
}

//! The Fortran API to dump the feature vector to a default path/filename
/*!
\param	iRvId	Pointer to the ID (1-based) of the random variable
\sa		ITL_dump_bound_block_global_entropy
*/
extern "C"
void
itl_dump_bound_block_global_entropy_2tmp_
(
	int *piRvId
)
{
	ITL_dump_bound_block_global_entropy
	(
		*piRvId - 1,
		szGlobalEntropyLogPathFilename
	);
}

/////////////////////////////////////////////////////////////////////
//! The C/C++ API to compute and dump the local entropy of the bound block to a file
/*!
\param	iRvId							ID (0-based) of the random variable
\param	szGlobalEntropyLogPathFilename	Path/filename of the file
*/
void
ITL_dump_bound_block_local_entropy
(
	const int iRvId,
	const int iDim,
	const double pdNeighborhood[],
	const char* szLocalEntropyLogPathFilename
)
{
	::pcBoundRandomField->_ComputeEntorpyFieldInBoundBlock
	(
		iRvId,
		iDim,
		pdNeighborhood,
		szLocalEntropyLogPathFilename
	);
}

//! The Fortran API to dump the feature vector to a default path/filename
/*!
\param	iRvId	Pointer to the ID (1-based) of the random variable
\sa		ITL_dump_bound_block_global_entropy
*/
extern "C"
void
itl_dump_bound_block_local_entropy3_2tmp_
(
	int *piRvId,
	double *pdXNeighborhood,
	double *pdYNeighborhood,
	double *pdZNeighborhood
)
{
	double pdNeighborhood[3];
	pdNeighborhood[0] = *pdXNeighborhood;
	pdNeighborhood[1] = *pdYNeighborhood;
	pdNeighborhood[2] = *pdZNeighborhood;
	char szLocalEntropyLogPathFilename[1024];
	sprintf(szLocalEntropyLogPathFilename, "dump/le.rank_%d.block_%d", ::iRank, ::pcBoundRandomField->IGetBoundBlock());

	ITL_dump_bound_block_local_entropy
	(
		*piRvId - 1,
		sizeof(pdNeighborhood)/sizeof(pdNeighborhood[0]),
		pdNeighborhood,
		szLocalEntropyLogPathFilename
	);
}

#endif
/*
 *
 * $Log$
 *
 */
