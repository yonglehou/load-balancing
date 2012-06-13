#include "TestingSystemTest.h"

#include "../ITestingSystem.h"
#include "../IMPICommunicator.h"

#include "../TestingSystem.h"

#include "utils/TestCommunicator.h"
#include "utils/TestInputFile.h"
#include "utils/TestProblemBuilder.h"
#include "utils/Assert.h"
#include <agents.h>

using namespace Concurrency;

void TestingSystemLoadTest()
{
	const char* filename = "C:\\StartMatrix\\matrix"; 
	FILE* file;
	fopen_s(&file, filename, "rb"); 

	//      7 8
	//      3 3
	//
	//      0 1 4 6
	//      0 1 4 7
	//
	//      5   6  | 7    11   100 | 200  305  40
	//      4   15 | 23   44   5   | 6    8    100
	//      ---------------------------------------
	//      1   0  | 156  346  67  | 5    46   55
	//      500 30 | 56   89   65  | 12   4    8
	//      3   45 | 67   78   888 | 567  16   900
	//      ---------------------------------------
	//      34  67 | 89   60   50  | 40   30   300
	//      67  56 | 44   23   45  | 78   60   90


	int bpNumberI = 2;
	int bpNumberJ = 2;
	
	int expectedSolutionI[] = {-1, 1, 4, 6};
	int expectedSolutionJ[] = {-1, 1, 4, 7};
	
	int mpiRank = 4;

	
	int steps = 10;

	int procI = mpiRank / (bpNumberI + 1);
	int procJ = mpiRank % (bpNumberJ + 1);
	
	int localWidth  = expectedSolutionJ[procJ + 1] - expectedSolutionJ[procJ];
	int localHeight = expectedSolutionI[procI + 1] - expectedSolutionI[procI];
	
	int expectedLocalMatrix[] = {
								 156,  346,  67,
								 56,   89,   65,
								 67,   78,   888
								};

	auto comm = TestCommunicator(
		[bpNumberI, bpNumberJ](int* size) -> int
		{
			*size = (bpNumberI + 1) * (bpNumberJ + 1);
			return MPI_SUCCESS;
		},
		[mpiRank](int* rank) -> int
		{
			*rank = mpiRank;
			return MPI_SUCCESS;
		},
		[](void* buf, int count, MPI_Datatype datatype, int dest, int tag) -> int
		{
			assert(false);
			return MPI_SUCCESS;
		},
		[](void* buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Status* status) -> int
		{
			assert(false);
			return MPI_SUCCESS;
		}
	);
	
	bool setBreakPointCountCalled = false;
	bool createSolutionICalled = false;
	bool createSolutionJCalled = false;
	bool createLocalMatrixCalled = false;
	
	int* solutionI = new int[bpNumberI + 2];
	int* solutionJ = new int[bpNumberJ + 2];
	double* localMatrix = new double[localWidth * localHeight];
	
	
	auto pb = TestProblemBuilder(
		[&setBreakPointCountCalled, bpNumberI, bpNumberJ](int actual_bpNumberI, int actual_bpNumberJ)
		{
			assert(!setBreakPointCountCalled);
			assert(bpNumberI == actual_bpNumberI);
			assert(bpNumberJ == actual_bpNumberJ);
			setBreakPointCountCalled = true;
		},
		[&setBreakPointCountCalled, &createSolutionICalled, solutionI]() -> int*
		{
			assert(setBreakPointCountCalled);
			assert(!createSolutionICalled);
			
			createSolutionICalled = true;
			return solutionI;
		},
		[&setBreakPointCountCalled, &createSolutionJCalled, solutionJ]() -> int*
		{
			assert(setBreakPointCountCalled);
			assert(!createSolutionJCalled);
			
			createSolutionJCalled = true;
			return solutionJ;
		},
		[&setBreakPointCountCalled, &createSolutionICalled, &createSolutionJCalled, &createLocalMatrixCalled, localMatrix]() -> double*
		{
			assert(createSolutionICalled);
			assert(createSolutionJCalled);
			assert(!createLocalMatrixCalled);

			createLocalMatrixCalled = true;
			return localMatrix;
		}
	);
	
	auto f = TestInputFile(
		[file](void *buffer, size_t elementSize, size_t count)
		{
			return fread(buffer, elementSize, count, file);
		},
		[file](long offset, int origin)
		{
			return fseek(file, offset, origin);
		}
	);

	auto ts = TestingSystem(f, steps);
	
	ts.LoadProblem(comm, pb);
	
	for(int i = 0; i < bpNumberI + 1; i++)
	{
		assert(solutionI[i] == expectedSolutionI[i]);
	}
	
	for(int i = 0; i < bpNumberJ + 1; i++)
	{
		assert(solutionJ[i] == expectedSolutionJ[i]);
	}
	
	for(int i = 0; i < localHeight; i++)
	{
		for(int j = 0; j < localWidth; j++)
		{
			assert(expectedLocalMatrix[i * localWidth + j] == localMatrix[i * localWidth + j]);
		}
	}

	fclose(file);
}


class TestMPIProcessor : public agent
{
private:
	struct Message
	{
		Message(void* data, int size)
			: data(data)
			, size(size)
		{
		}

		void* data;
		int size;
		TestMPIProcessor* sender;
	};

public:
	typedef std::function<void()> RunFunction;

	TestMPIProcessor(RunFunction runImpl)
		: m_runImpl(runImpl)
	{
	}

	void run()
	{
		m_runImpl();

		done();
	}

	void Send(TestMPIProcessor* target, void* data, int size)
	{
		assert(this != target);
		assert(data != 0);
		assert(size > 0);
		e.reset();
		Message message(data, size);
		message.sender = this;
		send(target->buffer, message);
		e.wait();
	}

	void Receive(TestMPIProcessor* source, void* data, int size)
	{
		std::vector<Message> tmpBuffer;
		assert(this != source);
		assert(data != 0);
		assert(size > 0);

		while(true)
		{
			Message message = receive(buffer);
			if(message.sender == source)
			{
				source->e.set();
				assert(message.size == size);
				memcpy(data, message.data, size);
				break;
			}
			else
			{
				tmpBuffer.push_back(message);
			}
		}

		for(auto p = tmpBuffer.begin(); p < tmpBuffer.end(); p++)
		{
			buffer.enqueue(*p);
		}
	}
	
private:
	RunFunction m_runImpl;
	unbounded_buffer<Message> buffer;
	event e;
};

void TestingSystemStep()
{
	int steps = 1;

	double matrix[] = {
		5   , 6  , 7   , 11  , 100 , 200 , 305 , 40  ,
		4   , 15 , 23  , 44  , 5   , 6   , 8   , 100 ,
		1   , 0  , 156 , 346 , 67  , 5   , 46  , 55	 ,
		500 , 30 , 56  , 89  , 65  , 12  , 4   , 8	 ,
		3   , 45 , 67  , 78  , 888 , 567 , 16  , 900 ,
		34  , 67 , 89  , 60  , 50  , 40  , 30  , 300 ,
		67  , 56 , 44  , 23  , 45  , 78  , 60  , 90	 ,
	};
	
	int matrixWidth = 8;
	int matrixHeight = 7;

	// ������ A
	//  5    6  | 7    11   100 | 200  305  40
	//  4    15 | 23   44   5   | 6    8    100
	//  ----------------------------------------
	//  1    0  | 156  346  67  | 5    46   55
	//  500  30 | 56   89   65  | 12   4    8
	//  3    45 | 67   78   888 | 567  16   900
	//  ----------------------------------------
	//  34   67 | 89   60   50  | 40   30   300
	//  67   56 | 44   23   45  | 78   60   90
	

	int bpNumberI_A = 2;
	int bpNumberJ_A = 2;
	
	int solutionI_A[] = {-1, 1, 4, 6};
	int solutionJ_A[] = {-1, 1, 4, 7};
	
	double* newMatrix_A = new double[matrixWidth * matrixHeight];
	double* newMatrix_B = new double[matrixWidth * matrixHeight];
	

	
	// ������ B
	//  5   | 6   7   | 11   100  200 | 305  40
	//  4   | 15  23  | 44   5    6   | 8    100
	//  1   | 0   156 | 346  67   5   | 46   55
	//  -----------------------------------------
	//  500 | 30  56  | 89   65   12  | 4    8
	//  3   | 45  67  | 78   888  567 | 16   900
	//  34  | 67  89  | 60   50   40  | 30   300
	//  -----------------------------------------
	//  67  | 56  44  | 23   45   78  | 60   90
	
	int bpNumberI_B = 2;
	int bpNumberJ_B = 3;
				  
	int solutionI_B[] = {-1, 2, 5, 6};
	int solutionJ_B[] = {-1, 0, 2, 5, 7};

	auto run = [steps, matrixWidth, matrixHeight](int bpNumberI, int bpNumberJ, int* solutionI, int* solutionJ, double* matrix)
	{
		auto _matrixWidth  = matrixWidth;
		auto _matrixHeight = matrixHeight;

		int mpiCommSize = (bpNumberI + 1) * (bpNumberJ + 1);
		
		auto f = TestInputFile(
			[](void *buffer, size_t elementSize, size_t count) -> size_t
			{
				assert(false);
				return 0;
			},
			[](long offset, int origin) -> int
			{
				assert(false);
				return 0;
			}
		);
		
		auto testingSystems = new TestingSystem*[mpiCommSize];
		auto processors = new TestMPIProcessor*[mpiCommSize];
		
		for(int procI = 0; procI < bpNumberI + 1; procI++)
		{
			for(int procJ = 0; procJ < bpNumberJ + 1; procJ++)
			{
				testingSystems[procI * (bpNumberJ + 1) + procJ] = new TestingSystem(f, steps);
			}
		}

		for(int step = 0; step < steps; step++)
		{
			for(int procI = 0; procI < bpNumberI + 1; procI++)
			{
				for(int procJ = 0; procJ < bpNumberJ + 1; procJ++)
				{
					int mpiRank = procI * (bpNumberJ + 1) + procJ;

					processors[mpiRank] = new TestMPIProcessor([mpiCommSize, mpiRank, procI, procJ, processors, solutionI, solutionJ, bpNumberI, bpNumberJ, testingSystems, matrix, _matrixWidth, _matrixHeight]()
					{
						auto _mpiCommSize = mpiCommSize;
						auto _mpiRank = mpiRank;
						auto _processors = processors;

						auto comm = TestCommunicator(
							[_mpiCommSize](int* size) -> int
							{
								*size = _mpiCommSize;
								return MPI_SUCCESS;
							},
							[_mpiRank](int* rank) -> int
							{
								*rank = _mpiRank;
								return MPI_SUCCESS;
							},
							[_mpiRank, _processors](void* buf, int count, MPI_Datatype datatype, int dest, int tag) -> int
							{
								_processors[_mpiRank]->Send(_processors[dest], buf, count * sizeof(int));
								
								return MPI_SUCCESS;
							},
							[_mpiRank, _processors](void* buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Status* status) -> int
							{
								_processors[_mpiRank]->Receive(_processors[source], buf, count * sizeof(int));
								
								return MPI_SUCCESS;
							}
						);
						
						int localWidth  = solutionJ[procJ + 1] - solutionJ[procJ];
						int localHeight = solutionI[procI + 1] - solutionI[procI];

						int* time_matrix = new int[localWidth * localHeight];
						double* local_matrix = new double[localWidth * localHeight];
						double* new_local_matrix = new double[localWidth * localHeight];
						
						int globalOffsetI = solutionI[procI] + 1;
						int globalOffsetJ = solutionJ[procJ] + 1;

						for(int i = 0; i < localHeight; i++)
						{
							for(int j = 0; j < localWidth; j++)
							{
								local_matrix[i * localWidth + j] = matrix[(globalOffsetI + i) * _matrixWidth + j + globalOffsetJ];
							}
						}

						//testingSystems[mpiRank]->Run(comm, time_matrix, matrix, newMatrix, solutionI, solutionJ, bpNumberI, bpNumberJ);
						testingSystems[mpiRank]->Run(comm, time_matrix, local_matrix, new_local_matrix, solutionI, solutionJ, bpNumberI, bpNumberJ);
						
						
						for(int i = 0; i < localHeight; i++)
						{
							for(int j = 0; j < localWidth; j++)
							{
								matrix[(globalOffsetI + i) * _matrixWidth + j + globalOffsetJ] = new_local_matrix[i * localWidth + j];
							}
						}

						//free(time_matrix);
						//free(local_matrix);
						//free(new_local_matrix);
					});

					processors[mpiRank]->start();
				}
			}

			for(int procI = 0; procI < bpNumberI + 1; procI++)
			{
				for(int procJ = 0; procJ < bpNumberJ + 1; procJ++)
				{
					int mpiRank = procI * (bpNumberJ + 1) + procJ;
					
					agent::wait(processors[mpiRank]);
				}
			}
		}
	};
	
	memcpy(newMatrix_A, matrix, sizeof(double) * matrixWidth * matrixHeight);
	memcpy(newMatrix_B, matrix, sizeof(double) * matrixWidth * matrixHeight);

	run(bpNumberI_A, bpNumberJ_A, solutionI_A, solutionJ_A, newMatrix_A);
	run(bpNumberI_B, bpNumberJ_B, solutionI_B, solutionJ_B, newMatrix_B);

	
	for(int i = 0; i < matrixHeight; i++)
	{
		for(int j = 0; j < matrixWidth; j++)
		{
			assert(newMatrix_A[i * matrixWidth + j] == newMatrix_B[i * matrixWidth + j]);
		}
	}
}