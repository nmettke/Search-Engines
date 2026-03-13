#pragma once

// Find the most positive subsequence of an array of N integers.
// Return the sum and the left and right indices of the subsequence.

// For example, for the sequence
//
//   { -1, 3, 5, 6, -2, -4, 1, 7, -15, 12, 7, -5 }
//
// the best sum = 20, left = 1, right = 10.

int MostPositiveSubsequence(const int a[], const int n, int &bestL, int &bestR);
