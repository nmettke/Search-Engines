#include <cstdlib>
#include <iostream>
#include <vector>

using namespace std;

int MostPositiveSubsequence(const int a[], const int n, int &bestL, int &bestR);

int main(int argc, char *argv[]) {
    ios::sync_with_stdio(false);

    if (argc < 2) {
        cerr << "Usage:  BestSubsequence <sequence of integers>\n";
        return 1;
    }

    int n = argc - 1;
    int *a = new int[n];

    for (int i = 0; i < n; ++i) {
        a[i] = atoi(argv[i + 1]);
    }

    int bestL = 0;
    int bestR = 0;
    int bestSum = MostPositiveSubsequence(a, n, bestL, bestR);

    cout << "Best sum = " << bestSum << " = a[ " << bestL << " .. " << bestR << " ] = { ";

    for (int i = bestL; i <= bestR; ++i) {
        if (i > bestL)
            cout << ", ";
        cout << a[i];
    }

    cout << " }\n";

    delete[] a;
    return 0;
}
