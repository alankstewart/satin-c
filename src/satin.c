/* Program satin.c by Alan K Stewart
 Saturation Intensity Calculation */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include "satin.h"

#define RAD   18E-2
#define W1    3E-1
#define DR    2E-3
#define DZ    4E-2
#define LAMDA 10.6E-3
#define AREA  (M_PI * (RAD * RAD))
#define Z1    (M_PI * (W1 * W1) / LAMDA)
#define Z12   Z1 * Z1
#define EXPR  2 * M_PI * DR
#define INCR  8001

int main(int argc, char* argv[]) {

    struct timeval t1, t2;
    double elapsedTime;

    gettimeofday(&t1, NULL);
    calculate(argc > 1 && strcmp(argv[1], "-concurrent") == 0);
    gettimeofday(&t2, NULL);

    elapsedTime = t2.tv_sec - t1.tv_sec + (t2.tv_usec - t1.tv_usec) / 1E6;
    printf("The time was %6.3f seconds.\n", elapsedTime);
    return EXIT_SUCCESS;
}

void calculate(_Bool concurrent) {

    int i, pNum, *inputPowers, lNum;
    laser *laserData;

    pNum = getInputPowers(&inputPowers);
    lNum = getLaserData(&laserData);

    pthread_t threads[lNum];
    satin_process_args process_args[lNum];

    for (i = 0; i < lNum; i++) {
        process_args[i].pNum = pNum;
        process_args[i].inputPowers = inputPowers;
        process_args[i].laserData = laserData[i];

        if (concurrent) {
            pthread_create(&threads[i], NULL, process, &process_args[i]);
        } else {
            process(&process_args[i]);
        }
    }

    if (concurrent) {
        for (i = 0; i < lNum; i++) {
            if (pthread_join(threads[i], NULL) != 0) {
                printf("Failed to join threads\n");
                error();
            }
        }
    }

    free(inputPowers);
    free(laserData);
}

int getInputPowers(int **inputPowers) {

    int i = 0, size, *ptr;
    char *inputPowerFile = "pin.dat";
    FILE *fd;

    size = N;
    if ((ptr = malloc(size * sizeof(int))) == NULL) {
        printf("Failed to allocate memory\n");
        error();
    }

    if ((fd = fopen(inputPowerFile, "r")) == NULL) {
        printf("Error opening %s\n", inputPowerFile);
        error();
    }

    while (fscanf(fd, "%d\n", &ptr[i]) != EOF) {
        i++;
        if (i >= size) {
            if ((ptr = realloc(ptr, (size *= 2) * sizeof(int))) == NULL) {
                printf("Failed to reallocate memory\n");
                error();
            }
        }
    }
    *inputPowers = ptr;

    if (fclose(fd) == EOF) {
        printf("Error closing %s\n", inputPowerFile);
        error();
    }

    return i;
}

int getLaserData(laser **laserData) {

    int i = 0, size;
    char *laserDataFile = "laser.dat";
    laser *ptr;
    FILE *fd;

    size = N;
    if ((ptr = malloc(size * sizeof(laser))) == NULL) {
        printf("Failed to allocate memory\n");
        error();
    }

    if ((fd = fopen(laserDataFile, "r")) == NULL) {
        printf("Error opening %s\n", laserDataFile);
        error();
    }

    while (fscanf(fd, "%s %f %d %s\n", ptr[i].outputFile, &ptr[i].smallSignalGain, &ptr[i].dischargePressure, ptr[i].carbonDioxide) != EOF) {
        i++;
        if (i >= size) {
            if ((ptr = realloc(ptr, (size *= 2) * sizeof(laser))) == NULL) {
                printf("Failed to reallocate memory\n");
                error();
            }
        }
    }
    *laserData = ptr;

    if (fclose(fd) == EOF) {
        printf("Error closing %s\n", laserDataFile);
        error();
    }

    return i;
}

void *process(void *arg) {

    int i, j;
    time_t the_time;
    satin_process_args* process_args = (satin_process_args*) arg;
    laser laserData = process_args->laserData;
    char *outputFile = laserData.outputFile;
    FILE *fd;

    if ((fd = fopen(outputFile, "w+")) == NULL) {
        printf("Error opening %s\n", outputFile);
        error();
    }

    time(&the_time);
    fprintf(fd,
            "Start date: %s\nGaussian Beam\n\nPressure in Main Discharge = %dkPa\nSmall-signal Gain = %4.1f\nCO2 via %s\n\nPin\t\tPout\t\tSat. Int\tln(Pout/Pin)\tPout-Pin\n(watts)\t\t(watts)\t\t(watts/cm2)\t\t\t(watts)\n",
            ctime(&the_time), laserData.dischargePressure, laserData.smallSignalGain, laserData.carbonDioxide);

    for (i = 0; i < process_args->pNum; i++) {
        gaussian *gaussianData;
        gaussianCalculation(process_args->inputPowers[i], laserData.smallSignalGain, &gaussianData);
        for (j = 0; j < sizeof(*gaussianData); j++) {
            int inputPower = gaussianData[j].inputPower;
            double outputPower = gaussianData[j].outputPower;
            fprintf(fd, "%d\t\t%7.3f\t\t%d\t\t%5.3f\t\t%7.3f\n", inputPower, outputPower,
                    gaussianData[j].saturationIntensity, log(outputPower / inputPower), outputPower - inputPower);
        }
        free(gaussianData);
    }

    time(&the_time);
    fprintf(fd, "\nEnd date: %s\n", ctime(&the_time));
    fflush(fd);

    if (fclose(fd) == EOF) {
        printf("Error closing %s\n", outputFile);
        error();
    }

    return NULL;
}

void gaussianCalculation(int inputPower, float smallSignalGain, gaussian **gaussianData) {

    int i, j, saturationIntensity;
    float r;
    double *expr1;
    gaussian *gaussians;

    if ((gaussians = malloc(16 * sizeof(gaussian))) == NULL) {
        printf("Failed to allocate memory\n");
        error();
    }

    if ((expr1 = malloc(INCR * sizeof(double))) == NULL) {
        printf("Failed to allocate memory\n");
        error();
    }

    for (i = 0; i < INCR; i++) {
        double zInc = ((double) i - 4000) / 25;
        expr1[i] = zInc * 2 * DZ / (Z12 + pow(zInc, 2));
    }

    double inputIntensity = 2 * inputPower / AREA;
    double expr2 = (smallSignalGain / 32E3) * DZ;

    i = 0;
    for (saturationIntensity = 10E3; saturationIntensity <= 25E3; saturationIntensity += 1E3) {
        double outputPower = 0.0;
        double expr3 = saturationIntensity * expr2;
        for (r = 0.0; r <= 0.5f; r += DR) {
            double outputIntensity = inputIntensity * exp(-2 * pow(r, 2) / pow(RAD, 2));
            for (j = 0; j < INCR; j++) {
                outputIntensity *= (1 + expr3 / (saturationIntensity + outputIntensity) - expr1[j]);
            }
            outputPower += (outputIntensity * EXPR * r);
        }
        gaussians[i].inputPower = inputPower;
        gaussians[i].saturationIntensity = saturationIntensity;
        gaussians[i].outputPower = outputPower;
        i++;
    }
    *gaussianData = gaussians;

    free(expr1);
}

void error() {

    exit(EXIT_FAILURE);
}
