/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    if (N == 32 && M == 32) {
        for (int ii = 0; ii < N; ii += 8) {
            for (int jj = 0; jj < M; jj += 8) {
                for (int i = ii; i < ii + 8; i++) {
                    int flag = 0;
                    int temp;
                    for (int j = jj; j < jj + 8; j++) {
                        if (i == j) {
                            temp = A[i][j];
                            flag = 1;
                        } else {
                            B[j][i] = A[i][j];
                        }   
                    }
                    if (flag == 1) B[i][i] = temp;
                }
            }
        }
    }
    
    if (N == 64 && M == 64) {
        for (int ii = 0; ii < N; ii += 8) {
            for (int jj = 0; jj < M; jj += 8) {
                for (int i = ii; i < ii + 4; i++) {
                    int a0 = A[i][jj];
                    int a1 = A[i][jj + 1];
                    int a2 = A[i][jj + 2];
                    int a3 = A[i][jj + 3];
                    int a4 = A[i][jj + 4];
                    int a5 = A[i][jj + 5];
                    int a6 = A[i][jj + 6];
                    int a7 = A[i][jj + 7];
                    B[jj][i] = a0;
                    B[jj + 1][i] = a1;
                    B[jj + 2][i] = a2;
                    B[jj + 3][i] = a3;
                    B[jj][i + 4] = a4;
                    B[jj + 1][i + 4] = a5;
                    B[jj + 2][i + 4] = a6;
                    B[jj + 3][i + 4] = a7;
                }
                for (int j = jj; j < jj + 4; j++) {
                    int t0 = B[j][ii + 4];
                    int t1 = B[j][ii + 5];
                    int t2 = B[j][ii + 6];
                    int t3 = B[j][ii + 7];

                    int a0 = A[ii + 4][j];
                    int a1 = A[ii + 5][j];
                    int a2 = A[ii + 6][j];
                    int a3 = A[ii + 7][j];
                    
                    B[j][ii + 4] = a0;
                    B[j][ii + 5] = a1;
                    B[j][ii + 6] = a2;
                    B[j][ii + 7] = a3;
                    
                    B[j + 4][ii] = t0;
                    B[j + 4][ii + 1] = t1;
                    B[j + 4][ii + 2] = t2;
                    B[j + 4][ii + 3] = t3; 

                    a0 = A[ii + 4][j + 4];
                    a1 = A[ii + 5][j + 4];
                    a2 = A[ii + 6][j + 4];
                    a3 = A[ii + 7][j + 4];
                    B[j + 4][ii + 4] = a0;
                    B[j + 4][ii + 5] = a1;
                    B[j + 4][ii + 6] = a2;
                    B[j + 4][ii + 7] = a3;
                }
            }
        }
    }

    // if (M == 61 && N == 67) {
    //     for (int ii = 0; ii < 64; ii += 8) {
    //         for (int jj = 0; jj < 56; jj += 8) {
    //             for (int i = ii; i < ii + 4; i++) {
    //                 int a0 = A[i][jj];
    //                 int a1 = A[i][jj + 1];
    //                 int a2 = A[i][jj + 2];
    //                 int a3 = A[i][jj + 3];
    //                 int a4 = A[i][jj + 4];
    //                 int a5 = A[i][jj + 5];
    //                 int a6 = A[i][jj + 6];
    //                 int a7 = A[i][jj + 7];
    //                 B[jj][i] = a0;
    //                 B[jj + 1][i] = a1;
    //                 B[jj + 2][i] = a2;
    //                 B[jj + 3][i] = a3;
    //                 B[jj][i + 4] = a4;
    //                 B[jj + 1][i + 4] = a5;
    //                 B[jj + 2][i + 4] = a6;
    //                 B[jj + 3][i + 4] = a7;
    //             }
    //             for (int j = jj; j < jj + 4; j++) {
    //                 int t0 = B[j][ii + 4];
    //                 int t1 = B[j][ii + 5];
    //                 int t2 = B[j][ii + 6];
    //                 int t3 = B[j][ii + 7];

    //                 int a0 = A[ii + 4][j];
    //                 int a1 = A[ii + 5][j];
    //                 int a2 = A[ii + 6][j];
    //                 int a3 = A[ii + 7][j];
                    
    //                 B[j][ii + 4] = a0;
    //                 B[j][ii + 5] = a1;
    //                 B[j][ii + 6] = a2;
    //                 B[j][ii + 7] = a3;
                    
    //                 B[j + 4][ii] = t0;
    //                 B[j + 4][ii + 1] = t1;
    //                 B[j + 4][ii + 2] = t2;
    //                 B[j + 4][ii + 3] = t3; 

    //                 a0 = A[ii + 4][j + 4];
    //                 a1 = A[ii + 5][j + 4];
    //                 a2 = A[ii + 6][j + 4];
    //                 a3 = A[ii + 7][j + 4];
    //                 B[j + 4][ii + 4] = a0;
    //                 B[j + 4][ii + 5] = a1;
    //                 B[j + 4][ii + 6] = a2;
    //                 B[j + 4][ii + 7] = a3;
    //             }
    //         }
    //     }
    //     for (int jj = 0; jj < 56; jj += 4) {
    //         for (int i = 64; i < 67; i++) {
    //             int a0 = A[i][jj];
    //             int a1 = A[i][jj + 1];
    //             int a2 = A[i][jj + 2];
    //             int a3 = A[i][jj + 3];
    //             B[jj][i] = a0;
    //             B[jj + 1][i] = a1;
    //             B[jj + 2][i] = a2;
    //             B[jj + 3][i] = a3;
    //         }
    //     }
    //     for (int ii = 0; ii < 64; ii += 4) {
    //         for (int i = ii; i < ii + 4; i++) {
    //             int a0 = A[i][56];
    //             int a1 = A[i][57];
    //             int a2 = A[i][58];
    //             int a3 = A[i][59];
    //             B[56][i] = a0;
    //             B[57][i] = a1;
    //             B[58][i] = a2;
    //             B[59][i] = a3;
    //         }
    //     }
    //     for (int ii = 0; ii < 64; ii += 4) {
    //         int a0 = A[ii][60];
    //         int a1 = A[ii + 1][60];
    //         int a2 = A[ii + 2][60];
    //         int a3 = A[ii + 3][60];
    //         B[60][ii] = a0;
    //         B[60][ii + 1] = a1;
    //         B[60][ii + 2] = a2;
    //         B[60][ii + 3] = a3;
    //     }
    //     for (int j = 56; j < 61; j++) {
    //         int a0 = A[64][j];
    //         int a1 = A[65][j];
    //         int a2 = A[66][j];
    //         B[j][64] = a0;
    //         B[j][65] = a1;
    //         B[j][66] = a2;
    //     }
    // }
    if (M == 61 && N == 67) {
        for (int ii = 0; ii < N; ii += 16) {
            for (int jj = 0; jj < M; jj += 16) {
                for (int i = ii; i < ii + 16 && i < N; i++) {
                    int flag = 0;
                    int temp;
                    for (int j = jj; j < jj + 16 && j < M; j++) {
                        if (i == j) {
                            temp = A[i][j];
                            flag = 1;
                        } else {
                            B[j][i] = A[i][j];
                        }   
                    }
                    if (flag == 1) B[i][i] = temp;
                }
            }
        }
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */ 

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc); 

}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

