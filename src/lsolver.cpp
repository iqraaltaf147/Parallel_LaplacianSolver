#include "lsolver.h"

#include <cmath>
#include <random>
#include <cassert>
#include <numeric>
#include <iostream>
#include <algorithm>

template <typename T>
inline T max(int n, const T *a) {
    return *std::max_element(a, a + n);
}

template <typename T>
inline T sum(int n, const T *a) {
    return std::accumulate(a, a + n, (T) 0);
}

template <typename T>
void initNewMemory2d(int n, T*** A) {
    *A = new T*[n];
    for (int i = 0; i < n; ++i) {
        (*A)[i] = new T[n];
    }
}

template <typename T>
void del(int n, T **P) {
    for (int i = 0; i < n; ++i) {
        delete[] P[i];
    }
    delete[] P;
}

void Lsolver::solve(const Graph *g, const double *b, double **x) {

    double *eta = NULL;
    auto beta = computeEtaAtStationarity(g, b, &eta);

    assert(eta != NULL);
    assert(beta > 0);

    computeCanonicalSolution(g, b, eta, beta, x);
    delete[] eta; eta = NULL;


    std::cerr << "Beta: " << beta << "\n";
}

void Lsolver::computeJ(int n, const double *b, double *J) {
    for (int i = 0; i < n; ++i) {
        J[i] = -b[i]/b[n - 1];
    }
}

static unsigned long x = 123456789;
static unsigned long y = 362436069;
static unsigned long z = 521288629;

unsigned long xorshf96() {
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}

const unsigned long long int MAX = 18446744073709551615ULL;
inline double random_double() {
    return (double) xorshf96()/MAX;
}

inline bool trueWithProbability(double p) {
    return random_double() <= p;
}

int Lsolver::pickRandomNeighbor(
        int n, const double *alias, const double *prob) {

    int col = (int) (random_double()*n);
    return (random_double() < prob[col]) ? col : alias[col];
}

#define MAX_EPOCHS 1000
#define LENGTH_OF_EPOCH 1000
void Lsolver::estimateEta(
        int n, double **alias, double **prob, int *cnt,
        int *Q, int *inQ, double beta, const double *J, double *eta) {

    for (int i = 0; i < n; ++i) {
        Q[i] = 0;
        cnt[i] = 0;
        inQ[i] = 0;
    }


    int epoch = 0;
    double oldC = 0, newC = 0;
    do {
        ++epoch;
        oldC = newC;
        for (int t = 0; t < LENGTH_OF_EPOCH; ++t) {
            for (int i = 0; i < n - 1; ++i) {
                Q[i] += trueWithProbability(beta * J[i]);
                if (Q[i]) {
                    --Q[i];
                    ++cnt[i];
                    ++inQ[pickRandomNeighbor(n, alias[i], prob[i])];
                }
            }

            for (int i = 0; i < n; ++i) {
                Q[i] += inQ[i];
                inQ[i] = 0;
            }
        }
        newC = (double) Q[n - 1]/(1 + sum(n, Q));
    } while (fabs(oldC - newC) > 1e-4 and epoch < MAX_EPOCHS);

    double T = epoch * LENGTH_OF_EPOCH;
    for (int i = 0; i < n - 1; ++i) {
        eta[i] = cnt[i]/T;
    }
    eta[n - 1] = 0;
}

void AliasMethod(int n, double *P, double *alias, double *prob) {
    int *small = new int[n];
    int *large = new int[n];

    int stop = 0, ltop = 0;
    for (int i = 0; i < n; ++i) {
        P[i] *= n;
        if (P[i] < 1) {
            small[stop++] = i;
        } else {
            large[ltop++] = i;
        }
    }

    while (stop > 0 and ltop > 0) {
        auto less = small[--stop];
        auto more = large[--ltop];

        prob[less] = P[less];
        alias[less] = more;

        P[more] -= (1 - P[less]);

        if (P[more] < 1) {
            small[stop++] = more;
        } else {
            large[ltop++] = more;
        }
    }

    while (ltop > 0) prob[large[--ltop]] = 1;
    while (stop > 0) prob[small[--stop]] = 1;

    delete[] small;
    delete[] large;
}


void Lsolver::computeAliasAndProb(
        int n, const Graph *g,
        double **alias, double **prob) {

    double **P = NULL;
    initNewMemory2d(n, &P);
    g->copyTransitionMatrix(P);

    for (int i = 0; i < n; ++i) {
        AliasMethod(n, P[i], alias[i], prob[i]);
    }

    del(n, P);
}

double Lsolver::computeEtaAtStationarity(
        const Graph *g, const double *b, double **eta) {

    int n = g->getNumVertex();

    double *J = new double[n];
    computeJ(n, b, J);

    double **prob = NULL;
    initNewMemory2d(n, &prob);

    double **alias = NULL;
    initNewMemory2d(n, &alias);

    // Alias method to sample from discrete distribution
    // http://www.keithschwarz.com/darts-dice-coins/
    computeAliasAndProb(n, g, alias, prob);

    int *cnt  = new int[n];

    // since transmission is concurrent, we need separate inbox
    int *Q = new int[n];
    int *inQ = new int[n];

    *eta = new double[n];
    double max_eta = 0;

    // Can start with any big value, but beta < beta* is below 1
    double beta = 1.28;
    do {
        beta /= 2;

        estimateEta(n, alias, prob, cnt, Q, inQ, beta, J, *eta);

        max_eta = max(n, *eta);
    } while (max_eta > 0.75*(1 - e1 - e2) and beta > 0);

    del(n, prob);
    del(n, alias);
    delete[] J; J = NULL;
    delete[] Q; Q = NULL;
    delete[] cnt; cnt = NULL;
    delete[] inQ; inQ = NULL;

    return beta;
}

void Lsolver::computeCanonicalSolution(
        const Graph *g, const double *b,
        double *eta, double beta, double **x) {

    int n = g->getNumVertex();

    double* d = new double[n];
    g->copyDegreeMatrix(d);

    *x = new double[n];

    for (int i = 0; i < n; ++i) {
        (*x)[i] = (-b[n - 1]/beta) * (eta[i]/d[i]);
    }

    // centering for canonical solution
    auto avg_x = sum(n, *x)/n;
    for (int i = 0; i < n; ++i) {
        (*x)[i] -= avg_x;
    }

    delete[] d;
}
