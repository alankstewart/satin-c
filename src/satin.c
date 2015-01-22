/* Program satin.c by Alan K Stewart
 Saturation Intensity Calculation */

#include "satin.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#define PI    3.14159265358979323846264338327950288
#define RAD   18E-2
#define RAD2  (RAD * RAD)
#define W1    3E-1
#define DR    2E-3
#define DZ    4E-2
#define LAMDA 10.6E-3
#define AREA  (PI * RAD2)
#define Z1    (PI * (W1 * W1) / LAMDA)
#define Z12   (Z1 * Z1)
#define EXPR  (2 * PI * DR)
#define INCR  8001

int main(int argc, char* argv[])
{
    struct timeval t1;
    struct timeval t2;

    gettimeofday(&t1, NULL);
    if (argc > 1 && strcmp(argv[1], "-single") == 0) {
        calculate();
    } else {
        calculateConcurrently();
    }
    gettimeofday(&t2, NULL);

    double elapsedTime = t2.tv_sec - t1.tv_sec + (t2.tv_usec - t1.tv_usec) / 1E6;
    printf("The time was %.3f seconds.\n", elapsedTime);
    return EXIT_SUCCESS;
}

void calculate()
{
    int *inputPowers;
    laser *laserData;
    satin_process_args *process_args;

    int pNum = getInputPowers(&inputPowers);
    int lNum = getLaserData(&laserData);

    if ((process_args = malloc(lNum * sizeof(satin_process_args))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < lNum; i++) {
        process_args[i].pNum = pNum;
        process_args[i].inputPowers = inputPowers;
        process_args[i].laserData = laserData[i];
        process(&process_args[i]);
    }

    free(inputPowers);
    free(laserData);
    free(process_args);
}

void calculateConcurrently()
{
    int *inputPowers;
    laser *laserData;
    pthread_t *threads;
    satin_process_args *process_args;

    int pNum = getInputPowers(&inputPowers);
    int lNum = getLaserData(&laserData);

    if ((threads = malloc(lNum * sizeof(pthread_t))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    if ((process_args = malloc(lNum * sizeof(satin_process_args))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < lNum; i++) {
        process_args[i].pNum = pNum;
        process_args[i].inputPowers = inputPowers;
        process_args[i].laserData = laserData[i];
        pthread_create(&threads[i], NULL, process, &process_args[i]);
    }

    for (int i = 0; i < lNum; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("Failed to join threads");
            exit(EXIT_FAILURE);
        }
    }

    free(inputPowers);
    free(laserData);
    free(process_args);
    free(threads);
}

int getInputPowers(int **inputPowers)
{
    int i = 0;
    int j = 6;
    int *ptr;
    char *inputPowerFile = "pin.dat";
    FILE *fp;

    if ((fp = fopen(inputPowerFile, "r")) == NULL) {
        fprintf(stderr, "Error opening %s: %s\n", inputPowerFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((ptr = malloc(j * sizeof(int))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    while (fscanf(fp, "%d\n", &ptr[i]) != EOF) {
        i++;
        if (i == j) {
            if ((ptr = realloc(ptr, (j *= 2) * sizeof(int))) == NULL) {
                perror("Failed to reallocate memory");
                exit(EXIT_FAILURE);
            }
        }
    }

    *inputPowers = ptr;

    if (fclose(fp) == EOF) {
        fprintf(stderr, "Error closing %s: %s\n", inputPowerFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    return i;
}

#ifdef REGEX
int getLaserData(laser **laserData)
{
    int i = 0;
    int j = 9;
    char *laserDataFile = "laser.dat";
    char *pattern =
    "((md|pi)[a-z]{2}\\.out)[ ]+([0-9]{2}\\.[0-9])[ ]+([0-9]+)[ ]+(MD|PI)";
    regex_t compiled;
    size_t nmatch = 6;
    regmatch_t *matchptr;
    laser *ptr;
    FILE *fp;

    if ((fp = fopen(laserDataFile, "r")) == NULL) {
        fprintf(stderr, "Error opening %s: %s\n", laserDataFile,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((ptr = malloc(j * sizeof(laser))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    if ((matchptr = malloc(nmatch * sizeof(regmatch_t))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    int rc = regcomp(&compiled, pattern, REG_EXTENDED);
    if (rc != 0) {
        printf("Failed to compile regex: %d (%s)\n", rc,
                get_regerror(rc, &compiled));
        exit(rc);
    }

    int buf = 25;
    char *line;
    if ((line = malloc(buf * sizeof(char))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    while (fgets(line, buf, fp) != NULL) {
        rc = regexec(&compiled, line, nmatch, matchptr, 0);
        if (rc != 0) {
            printf("Failed to execute regex: %d (%s)\n", rc,
                    get_regerror(rc, &compiled));
            exit(rc);
        }

        line[matchptr[1].rm_eo] = 0;
        strcpy(ptr[i].outputFile, line + matchptr[1].rm_so);

        line[matchptr[3].rm_eo] = 0;
        ptr[i].smallSignalGain = atof(line + matchptr[3].rm_so);

        line[matchptr[4].rm_eo] = 0;
        ptr[i].dischargePressure = atoi(line + matchptr[4].rm_so);

        line[matchptr[5].rm_eo] = 0;
        strcpy(ptr[i].carbonDioxide, line + matchptr[5].rm_so);

        i++;
        if (i == j) {
            if ((ptr = realloc(ptr, (j *= 2) * sizeof(laser))) == NULL) {
                perror("Failed to reallocate memory");
                exit(EXIT_FAILURE);
            }
        }
    }

    *laserData = ptr;

    if (fclose(fp) == EOF) {
        fprintf(stderr, "Error closing %s: %s\n", laserDataFile,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("Parsed %d records of %s with regex\n", i, laserDataFile);

    regfree(&compiled);
    free(line);
    free(matchptr);
    return i;
}

char *get_regerror(int errcode, regex_t *compiled)
{
    size_t length = regerror(errcode, compiled, NULL, 0);
    char *buffer = malloc(length);
    (void) regerror(errcode, compiled, buffer, length);
    return buffer;
}

#else
int getLaserData(laser **laserData)
{
    int i = 0;
    int j = 9;
    char *laserDataFile = "laser.dat";
    laser *ptr;
    FILE *fp;

    if ((fp = fopen(laserDataFile, "r")) == NULL) {
        fprintf(stderr, "Error opening %s: %s\n", laserDataFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((ptr = malloc(j * sizeof(laser))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    while (fscanf(fp, "%s %lf %d %s\n", ptr[i].outputFile, &ptr[i].smallSignalGain, &ptr[i].dischargePressure,
            ptr[i].carbonDioxide) != EOF) {
        i++;
        if (i == j) {
            if ((ptr = realloc(ptr, (j *= 2) * sizeof(laser))) == NULL) {
                perror("Failed to reallocate memory");
                exit(EXIT_FAILURE);
            }
        }
    }

    *laserData = ptr;

    if (fclose(fp) == EOF) {
        fprintf(stderr, "Error closing %s: %s\n", laserDataFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    return i;
}
#endif

void *process(void *arg)
{
    time_t the_time;
    satin_process_args* process_args = (satin_process_args*) arg;
    laser laserData = process_args->laserData;
    char *outputFile = laserData.outputFile;
    gaussian *gaussians;
    FILE *fp;

    if ((fp = fopen(outputFile, "w+")) == NULL) {
        fprintf(stderr, "Error opening %s: %s\n", outputFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    time(&the_time);
    fprintf(fp,
            "Start date: %s\nGaussian Beam\n\nPressure in Main Discharge = %dkPa\nSmall-signal Gain = %4.1f\nCO2 via %s\n\nPin\t\tPout\t\tSat. Int\tln(Pout/Pin)\tPout-Pin\n(watts)\t\t(watts)\t\t(watts/cm2)\t\t\t(watts)\n",
            ctime(&the_time), laserData.dischargePressure, laserData.smallSignalGain, laserData.carbonDioxide);

    for (int i = 0; i < process_args->pNum; i++) {
        int gaussiansSize = gaussianCalculation(process_args->inputPowers[i], laserData.smallSignalGain, &gaussians);
        for (int j = 0; j < gaussiansSize; j++) {
            fprintf(fp, "%d\t\t%7.3f\t\t%d\t\t%5.3f\t\t%7.3f\n", gaussians[j].inputPower, gaussians[j].outputPower,
                    gaussians[j].saturationIntensity, gaussians[j].logOutputPowerDividedByInputPower,
                    gaussians[j].ouputPowerMinusInputPower);
        }
    }

    time(&the_time);
    fprintf(fp, "\nEnd date: %s\n", ctime(&the_time));
    fflush(fp);

    if (fclose(fp) == EOF) {
        fprintf(stderr, "Error closing %s: %s\n", outputFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    free(gaussians);
    return NULL;
}

int gaussianCalculation(int inputPower, float smallSignalGain, gaussian **gaussians)
{
    int k = 17;
    double *expr1;
    gaussian *gaussianData;

    if ((gaussianData = malloc(k * sizeof(gaussian))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    if ((expr1 = malloc(INCR * sizeof(double))) == NULL) {
        perror(ERR);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < INCR; i++) {
        double zInc = ((double) i - INCR / 2) / 25;
        expr1[i] = zInc * 2 * DZ / (Z12 + pow(zInc, 2));
    }

    double inputIntensity = 2 * inputPower / AREA;
    double expr2 = smallSignalGain / 32E3 * DZ;

    int i = 0;
    for (int saturationIntensity = 10E3; saturationIntensity <= 25E3; saturationIntensity += 1E3) {
        double expr3 = saturationIntensity * expr2;
        double outputPower = 0.0;
        for (double r = 0.0; r <= 0.5; r += DR) {
            double outputIntensity = inputIntensity * exp(-2 * pow(r, 2) / RAD2);
            for (int j = 0; j < INCR; j++) {
                outputIntensity *= (1 + expr3 / (saturationIntensity + outputIntensity) - expr1[j]);
            }
            outputPower += outputIntensity * EXPR * r;
        }
        gaussianData[i].inputPower = inputPower;
        gaussianData[i].saturationIntensity = saturationIntensity;
        gaussianData[i].outputPower = outputPower;
        gaussianData[i].logOutputPowerDividedByInputPower = log(outputPower / inputPower);
        gaussianData[i].ouputPowerMinusInputPower = outputPower - inputPower;
        i++;
        if (i == k) {
            if ((gaussianData = realloc(gaussianData, (k *= 2) * sizeof(gaussian))) == NULL) {
                perror("Failed to reallocate memory");
                exit(EXIT_FAILURE);
            }
        }
    }

    *gaussians = gaussianData;

    free(expr1);
    return i;
}
