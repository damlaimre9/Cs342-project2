#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // For usleep
#include <pthread.h>      // For pthreads
#include <sys/time.h>     // For gettimeofday
#include <math.h>         // For log function
#include <limits.h>       // For INT_MAX
#include <time.h>         // For time(NULL)

typedef struct burst {
    int pid;                    // Process ID
    int burst_length;           // Burst length in ms
    long arrival_time;          // Arrival time in ms since simulation start
    long finish_time;           // Finish time in ms since simulation start
    long turnaround_time;       // Turnaround time in ms
    int cpu_id;                 // CPU where the burst executed
    struct burst *next;         // Pointer to the next burst in the queue
} burst_t;

typedef struct queue {
    burst_t *head;              // Head of the burst queue
    pthread_mutex_t lock;       // Mutex lock for the queue
    pthread_cond_t cond;        // Condition variable for the queue
} queue_t;

struct timeval simulation_start_time;  // Simulation start time
queue_t *queues;                       // Array of ready queues
burst_t *finished_bursts = NULL;       // List of finished bursts
pthread_mutex_t finished_bursts_lock;  // Mutex for the finished bursts list
int N;                                 // Number of processors
char SAP;                              // Scheduling approach ('S' or 'M')
char QS[3];                            // Queue selection method ('RM', 'LM', 'NA')
char ALG[5];                           // Scheduling algorithm ('FCFS', 'SJF')
int OUTMODE = 1;                       // Output mode
int simulation_finished = 0;           // Flag to indicate simulation end

long get_current_time_ms() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    long seconds = current_time.tv_sec - simulation_start_time.tv_sec;
    long useconds = current_time.tv_usec - simulation_start_time.tv_usec;
    return (seconds * 1000) + (useconds / 1000);
}
int generate_random(int mean, int min, int max) {
    double lambda = 1.0 / mean;
    int x;
    do {
        double u = rand() / (double)RAND_MAX;
        double val = (-1.0) * log(1 - u) / lambda;
        x = (int)(val + 0.5); // Round to nearest integer
    } while (x < min || x > max);
    return x;
}

void *processor_thread(void *arg) {
    int cpu_id = *((int *)arg);               // CPU ID for this thread
    int queue_index = (SAP == 'S') ? 0 : cpu_id - 1;
    queue_t *queue = &queues[queue_index];    // Associated queue

    while (1) {
        pthread_mutex_lock(&queue->lock);
        while (queue->head == NULL) {
            if (simulation_finished) {
                pthread_mutex_unlock(&queue->lock);
                pthread_exit(NULL);           // Terminate if simulation is finished
            }
            pthread_cond_wait(&queue->cond, &queue->lock);  // Wait for bursts
        }

        // Select a burst according to the scheduling algorithm
        burst_t *selected_burst = NULL;
        if (strcmp(ALG, "FCFS") == 0) {
            // FCFS: Pick the head of the queue
            selected_burst = queue->head;
            queue->head = selected_burst->next;
        } else if (strcmp(ALG, "SJF") == 0) {
            // SJF: Pick the burst with the shortest burst length
            burst_t *curr = queue->head;
            burst_t *prev = NULL;
            burst_t *shortest_prev = NULL;
            int shortest_length = INT_MAX;
            while (curr != NULL) {
                if (curr->burst_length < shortest_length) {
                    shortest_length = curr->burst_length;
                    shortest_prev = prev;
                    selected_burst = curr;
                }
                prev = curr;
                curr = curr->next;
            }
            // Remove selected_burst from queue
            if (selected_burst == queue->head) {
                queue->head = selected_burst->next;
            } else {
                shortest_prev->next = selected_burst->next;
            }
        }
        pthread_mutex_unlock(&queue->lock);

        if (selected_burst != NULL) {
            selected_burst->cpu_id = cpu_id;  // Record CPU ID

            // Output based on OUTMODE
            if (OUTMODE == 2 || OUTMODE == 3) {
                long current_time = get_current_time_ms();
                printf("time=%ld, cpu=%d, pid=%d, burstlen=%d\n",
                       current_time, cpu_id, selected_burst->pid, selected_burst->burst_length);
            }

            // Simulate burst execution
            usleep(selected_burst->burst_length * 1000);

            // Record finish time and calculate turnaround time
            selected_burst->finish_time = get_current_time_ms();
            selected_burst->turnaround_time = selected_burst->finish_time - selected_burst->arrival_time;

            // Add burst to finished list
            pthread_mutex_lock(&finished_bursts_lock);
            selected_burst->next = finished_bursts;
            finished_bursts = selected_burst;
            pthread_mutex_unlock(&finished_bursts_lock);

            // Additional output for OUTMODE 3
            if (OUTMODE == 3) {
                printf("Burst pid=%d finished on cpu=%d at time=%ld\n",
                       selected_burst->pid, cpu_id, selected_burst->finish_time);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    // Variables for command-line arguments
    char *INFILE = NULL;
    int use_random = 0;
    int T, T1, T2, L, L1, L2, PC;
    char *OUTFILE = NULL;

    // Initialize mutex for finished bursts
    pthread_mutex_init(&finished_bursts_lock, NULL);

    // Parse command-line arguments
    int arg_index = 1;
    while (arg_index < argc) {
        if (strcmp(argv[arg_index], "-n") == 0) {
            N = atoi(argv[++arg_index]);
            arg_index++;
        } else if (strcmp(argv[arg_index], "-a") == 0) {
            arg_index++;
            SAP = argv[arg_index][0]; // 'S' or 'M'
            arg_index++;
            strcpy(QS, argv[arg_index]); // 'RM', 'LM', 'NA'
            arg_index++;
        } else if (strcmp(argv[arg_index], "-s") == 0) {
            arg_index++;
            strcpy(ALG, argv[arg_index]); // 'FCFS', 'SJF'
            arg_index++;
        } else if (strcmp(argv[arg_index], "-i") == 0) {
            arg_index++;
            INFILE = argv[arg_index];
            arg_index++;
        } else if (strcmp(argv[arg_index], "-r") == 0) {
            use_random = 1;
            arg_index++;
            T = atoi(argv[arg_index++]);
            T1 = atoi(argv[arg_index++]);
            T2 = atoi(argv[arg_index++]);
            L = atoi(argv[arg_index++]);
            L1 = atoi(argv[arg_index++]);
            L2 = atoi(argv[arg_index++]);
            PC = atoi(argv[arg_index++]);
        } else if (strcmp(argv[arg_index], "-m") == 0) {
            arg_index++;
            OUTMODE = atoi(argv[arg_index]);
            arg_index++;
        } else if (strcmp(argv[arg_index], "-o") == 0) {
            arg_index++;
            OUTFILE = argv[arg_index];
            arg_index++;
        } else {
            printf("Invalid argument: %s\n", argv[arg_index]);
            exit(1);
        }
    }

    // Validate arguments
    if (N < 1 || N > 64) {
        printf("Invalid number of processors. Must be between 1 and 64.\n");
        exit(1);
    }
    if (SAP == 'M' && N <= 1) {
        printf("For multi-queue approach, N must be greater than 1.\n");
        exit(1);
    }
    if (SAP == 'S' && strcmp(QS, "NA") != 0) {
        printf("For single-queue approach, QS must be 'NA'.\n");
        exit(1);
    }
    if (INFILE == NULL && !use_random) {
        printf("Either -i or -r option must be specified.\n");
        exit(1);
    }

    // Redirect output to file if specified
    if (OUTFILE != NULL) {
        freopen(OUTFILE, "w", stdout);
    }

    // Seed random number generator
    srand(time(NULL));

    // Initialize simulation start time
    gettimeofday(&simulation_start_time, NULL);

    // Initialize queues
    if (SAP == 'S') {
        queues = malloc(sizeof(queue_t));
        queues[0].head = NULL;
        pthread_mutex_init(&queues[0].lock, NULL);
        pthread_cond_init(&queues[0].cond, NULL);
    } else if (SAP == 'M') {
        queues = malloc(sizeof(queue_t) * N);
        for (int i = 0; i < N; i++) {
            queues[i].head = NULL;
            pthread_mutex_init(&queues[i].lock, NULL);
            pthread_cond_init(&queues[i].cond, NULL);
        }
    }

    // Create processor threads
    pthread_t *thread_ids = malloc(sizeof(pthread_t) * N);
    int *cpu_ids = malloc(sizeof(int) * N);
    for (int i = 0; i < N; i++) {
        cpu_ids[i] = i + 1; // CPU IDs start from 1
        pthread_create(&thread_ids[i], NULL, processor_thread, &cpu_ids[i]);
    }

    // Main thread processes input bursts
    if (INFILE != NULL) {
        // Read from input file
        FILE *fp = fopen(INFILE, "r");
        if (fp == NULL) {
            printf("Error opening input file.\n");
            exit(1);
        }
        char line[100];
        int pid = 1;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strncmp(line, "PL", 2) == 0) {
                int burst_length = atoi(line + 3);

                // Create a new burst item
                burst_t *new_burst = malloc(sizeof(burst_t));
                new_burst->pid = pid++;
                new_burst->burst_length = burst_length;
                new_burst->next = NULL;
                new_burst->arrival_time = get_current_time_ms();

                // Determine queue index
                int queue_index = 0;
                if (SAP == 'S') {
                    queue_index = 0;
                } else if (SAP == 'M') {
                    if (strcmp(QS, "RM") == 0) {
                        queue_index = (pid - 2) % N;
                    } else if (strcmp(QS, "LM") == 0) {
                        // Find queue with least load
                        int min_load = INT_MAX;
                        for (int i = 0; i < N; i++) {
                            pthread_mutex_lock(&queues[i].lock);
                            int load = 0;
                            burst_t *curr = queues[i].head;
                            while (curr != NULL) {
                                load += curr->burst_length;
                                curr = curr->next;
                            }
                            pthread_mutex_unlock(&queues[i].lock);
                            if (load < min_load) {
                                min_load = load;
                                queue_index = i;
                            }
                        }
                    }
                }

                // Add burst to queue
                pthread_mutex_lock(&queues[queue_index].lock);
                if (queues[queue_index].head == NULL) {
                    queues[queue_index].head = new_burst;
                } else {
                    burst_t *curr = queues[queue_index].head;
                    while (curr->next != NULL) {
                        curr = curr->next;
                    }
                    curr->next = new_burst;
                }
                pthread_cond_signal(&queues[queue_index].cond);
                pthread_mutex_unlock(&queues[queue_index].lock);

                // Output for OUTMODE 3
                if (OUTMODE == 3) {
                    printf("Added burst pid=%d to queue=%d at time=%ld\n",
                           new_burst->pid, queue_index + 1, new_burst->arrival_time);
                }
            } else if (strncmp(line, "IAT", 3) == 0) {
                int IAT = atoi(line + 4);
                usleep(IAT * 1000);  // Sleep for interarrival time
            }
        }
        fclose(fp);
    } else if (use_random) {
        // Generate random bursts
        int pid = 1;
        for (int i = 0; i < PC; i++) {
            int burst_length = generate_random(L, L1, L2);

            // Create a new burst item
            burst_t *new_burst = malloc(sizeof(burst_t));
            new_burst->pid = pid++;
            new_burst->burst_length = burst_length;
            new_burst->next = NULL;
            new_burst->arrival_time = get_current_time_ms();

            // Determine queue index
            int queue_index = 0;
            if (SAP == 'S') {
                queue_index = 0;
            } else if (SAP == 'M') {
                if (strcmp(QS, "RM") == 0) {
                    queue_index = (pid - 2) % N;
                } else if (strcmp(QS, "LM") == 0) {
                    // Find queue with least load
                    int min_load = INT_MAX;
                    for (int j = 0; j < N; j++) {
                        pthread_mutex_lock(&queues[j].lock);
                        int load = 0;
                        burst_t *curr = queues[j].head;
                        while (curr != NULL) {
                            load += curr->burst_length;
                            curr = curr->next;
                        }
                        pthread_mutex_unlock(&queues[j].lock);
                        if (load < min_load) {
                            min_load = load;
                            queue_index = j;
                        }
                    }
                }
            }

            // Add burst to queue
            pthread_mutex_lock(&queues[queue_index].lock);
            if (queues[queue_index].head == NULL) {
                queues[queue_index].head = new_burst;
            } else {
                burst_t *curr = queues[queue_index].head;
                while (curr->next != NULL) {
                    curr = curr->next;
                }
                curr->next = new_burst;
            }
            pthread_cond_signal(&queues[queue_index].cond);
            pthread_mutex_unlock(&queues[queue_index].lock);

            // Output for OUTMODE 3
            if (OUTMODE == 3) {
                printf("Added burst pid=%d to queue=%d at time=%ld\n",
                       new_burst->pid, queue_index + 1, new_burst->arrival_time);
            }

            // Generate and sleep for interarrival time
            int IAT = generate_random(T, T1, T2);
            usleep(IAT * 1000);
        }
    }

    // Indicate that simulation is finished
    simulation_finished = 1;

    // Wake up all processor threads
    if (SAP == 'S') {
        pthread_mutex_lock(&queues[0].lock);
        pthread_cond_broadcast(&queues[0].cond);
        pthread_mutex_unlock(&queues[0].lock);
    } else if (SAP == 'M') {
        for (int i = 0; i < N; i++) {
            pthread_mutex_lock(&queues[i].lock);
            pthread_cond_signal(&queues[i].cond);
            pthread_mutex_unlock(&queues[i].lock);
        }
    }

    // Wait for all processor threads to finish
    for (int i = 0; i < N; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    // Collect and print statistics
    // Count the number of bursts
    int num_bursts = 0;
    pthread_mutex_lock(&finished_bursts_lock);
    burst_t *curr = finished_bursts;
    while (curr != NULL) {
        num_bursts++;
        curr = curr->next;
    }
    pthread_mutex_unlock(&finished_bursts_lock);

    // Create an array of bursts
    burst_t **burst_array = malloc(sizeof(burst_t *) * num_bursts);
    int index = 0;
    pthread_mutex_lock(&finished_bursts_lock);
    curr = finished_bursts;
    while (curr != NULL) {
        burst_array[index++] = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&finished_bursts_lock);

    // Sort bursts by pid
    int compare_bursts(const void *a, const void *b) {
        burst_t *burst_a = *(burst_t **)a;
        burst_t *burst_b = *(burst_t **)b;
        return burst_a->pid - burst_b->pid;
    }
    qsort(burst_array, num_bursts, sizeof(burst_t *), compare_bursts);

    // Print burst information
    printf("pid cpu burstlen arv finish waitingtime turnaround\n");
    long total_turnaround_time = 0;
    for (int i = 0; i < num_bursts; i++) {
        burst_t *b = burst_array[i];
        long waiting_time = b->turnaround_time - b->burst_length;
        printf("%d %d %d %ld %ld %ld %ld\n",
               b->pid, b->cpu_id, b->burst_length, b->arrival_time,
               b->finish_time, waiting_time, b->turnaround_time);
        total_turnaround_time += b->turnaround_time;
    }
    double avg_turnaround_time = (double)total_turnaround_time / num_bursts;
    printf("average turnaround time: %.2f ms\n", avg_turnaround_time);

    // Free allocated memory
    free(burst_array);
    free(thread_ids);
    free(cpu_ids);
    if (SAP == 'S') {
        pthread_mutex_destroy(&queues[0].lock);
        pthread_cond_destroy(&queues[0].cond);
        free(queues);
    } else if (SAP == 'M') {
        for (int i = 0; i < N; i++) {
            pthread_mutex_destroy(&queues[i].lock);
            pthread_cond_destroy(&queues[i].cond);
        }
        free(queues);
    }

    // Close output file if necessary
    if (OUTFILE != NULL) {
        fclose(stdout);
    }

    return 0;
}

