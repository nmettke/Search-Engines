#include "MostPositiveSubsequence.h"

int MostPositiveSubsequence(const int a[], const int n, int &bestL, int &bestR) {
    int currSum = a[0];
    int currL = 0;

    int bestSum = a[0];
    int bestL0 = 0;
    int bestR0 = 0;
    int bestLen = 1;

    for (int i = 1; i < n; ++i) {
        int x = a[i];

        if (currSum <= 0) {
            currSum = x;
            currL = i;
        } else {
            currSum += x;
        }

        if (currSum > bestSum) {
            bestSum = currSum;
            bestL0 = currL;
            bestR0 = i;
            bestLen = i - currL + 1;
        } else if (currSum == bestSum) {
            int currLen = i - currL + 1;

            if (currLen < bestLen) {
                bestL0 = currL;
                bestR0 = i;
                bestLen = currLen;
            } else if (currLen == bestLen) {
                if (currL < bestL0) {
                    bestL0 = currL;
                    bestR0 = i;
                } else if (currL == bestL0 && i < bestR0) {
                    bestR0 = i;
                }
            }
        }
    }

    bestL = bestL0;
    bestR = bestR0;
    return bestSum;
}