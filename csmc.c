//
//  main.c
//  Seeking Tutor Problem
//
//  Created by Haoda LE on 2019/11/4.
//  Copyright © 2019 haoda le. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <assert.h>

//maximum student numbers
#define Max_stu_size 2000
//const int Max_stu_size=2000;

//check how many students have done.
//then coordinate and tutors threads can terminate
int done=0;
int totalRequests=0;
int totalSessions=0;
int tutoringNow=0;

void *student_thread(void *student_id);
void *tutor_thread(void *tutor_id);
void *coordinator_thread();
//void *coordinator_thread(void *);

int student_num=0;
int tutor_num=0;
int help_num=0;
int chair_num=0;
int occupied_chair_num=0;

int newArrivedStudentQueue[Max_stu_size];
int tutorFinishedQueue[Max_stu_size];
int priorityQueue[Max_stu_size][2];
int student_priority[Max_stu_size];
int student_ids[Max_stu_size];
int tutor_ids[Max_stu_size];

sem_t sem_student;
sem_t sem_coordinator;
//sem_t sem_tutorToStudent[Max_stu_size];
pthread_mutex_t seatLock;
pthread_mutex_t queueLock;
pthread_mutex_t tutorFinishedQueueLock;

int main(int argc, const char * argv[]) {

    //test
    /*student_num=10;
    tutor_num=3;
    chair_num=4;
    help_num=5;*/
    
    
    //get input argument
    if (argc != 5){
        printf("Usage: <# of Students> <# of tutors> <# of chairs> <# of help>\n");
        exit(-1);
    }
    
    student_num=atoi(argv[1]);
    tutor_num=atoi(argv[2]);
    chair_num=atoi(argv[3]);
    help_num=atoi(argv[4]);
    
    if(student_num > Max_stu_size || tutor_num > Max_stu_size){
        printf("Max student number is: %d; Max tutor number is: %d\n", Max_stu_size, Max_stu_size);
        exit(-1);
    }
    
    //init arrays
    int i;
    for(i=0;i<student_num;i++){
        newArrivedStudentQueue[i]=-1;
        tutorFinishedQueue[i]=-1;
        priorityQueue[i][0]=-1;
        priorityQueue[i][1]=-1;
        student_priority[i]=0;
    }
    
    //init lock and semaphore
    sem_init(&sem_student,0,0);
    sem_init(&sem_coordinator,0,0);
    pthread_mutex_init(&seatLock,NULL);
    pthread_mutex_init(&queueLock,NULL);
    pthread_mutex_init(&tutorFinishedQueueLock,NULL);
    /*for(i=0;i<student_num;i++){
        sem_init(&sem_tutorToStudent[i],0,0);
    }*/
    
    //allocate threads
    pthread_t students[student_num];
    pthread_t tutors[tutor_num];
    pthread_t coordinator;
    
    //create threads
    assert(pthread_create(&coordinator,NULL,coordinator_thread,NULL)==0);
    
    for(i = 0; i < student_num; i++)
    {
        student_ids[i] = i + 1;
        assert(pthread_create(&students[i], NULL, student_thread, (void*) &student_ids[i])==0);
    }
    
    for(i = 0; i < tutor_num; i++)
    {
        tutor_ids[i] = i + student_num + 1;
        assert(pthread_create(&tutors[i], NULL, tutor_thread, (void*) &tutor_ids[i])==0);
    }
    
    //join threads
    pthread_join(coordinator, NULL);
    
    for(i =0; i < student_num; i++)
    {
        pthread_join(students[i],NULL);
    }
    
    for(i =0; i < tutor_num; i++)
    {
        pthread_join(tutors[i],NULL);
    }
    
    return 0;
}

//students action.
//1.program;
//2.find the empty seat and nofity coordinator;
//3.wait tutor to wake it up and finish tutoring.
void *student_thread(void *student_id){
    int id_student=*(int*)student_id;
    
    while(1){
        if(student_priority[id_student-1]>=help_num) {
            
            pthread_mutex_lock(&seatLock);
            done++;
            pthread_mutex_unlock(&seatLock);

            //notify coordinate to terminate
            sem_post(&sem_student);
            
            //printf("------student %d terminate------\n",id_student);
            pthread_exit(NULL);
        }
        
        //programing
        //sleeping for a random amount of time up to 2 ms
        float programTime=(float)(rand()%200)/100;
        //printf("sleep time = %f.\n", programTime);
        usleep(programTime);
        
        pthread_mutex_lock(&seatLock);
        if(occupied_chair_num>=chair_num){
            //St: Student x found no empty chair. Will try again later
            printf("St: Student %d found no empty chair. Will try again later.\n",id_student);
            pthread_mutex_unlock(&seatLock);
            continue;
        }
        
        occupied_chair_num++;
        totalRequests++;
        newArrivedStudentQueue[id_student-1]=totalRequests;
        
        //St: Student x takes a seat. Empty chairs = <# of empty chairs>.
        printf("St: Student %d takes a seat. Empty chairs = %d\n",id_student,chair_num-occupied_chair_num);
        pthread_mutex_unlock(&seatLock);
        
        //notify coordinator that student seated.
        sem_post(&sem_student);
        
        //wait to be tutored.
        while(tutorFinishedQueue[id_student-1]==-1);
        //sem_wait(&sem_tutorToStudent[id_student-1]);

        //St: Student x received help from Tutor y.
        printf("St: Student %d received help from Tutor %d.\n",id_student,tutorFinishedQueue[id_student-1]-student_num);
        
        //reset the shared data
        pthread_mutex_lock(&tutorFinishedQueueLock);
        tutorFinishedQueue[id_student-1]=-1;
        pthread_mutex_unlock(&tutorFinishedQueueLock);
        
        //tutored times++ after tutoring.
        pthread_mutex_lock(&seatLock);
        student_priority[id_student-1]++;
        pthread_mutex_unlock(&seatLock);
    }
}

//tutors action
//1.wait coordinator's nofitication;
//2.find student from queue with highest priority;
//3.tutoring and notify student it's done;
void *tutor_thread(void *tutor_id){
    int id_tutor=*(int*)tutor_id;
    int studentTutoredTimes;
    //students with same tutored times, who come first has higher priority
    int studentSequence;
    int id_student;
    
    while(1){
        //if all students finish, tutors threads terminate.
        if(done==student_num){
            //printf("------tutor %d terminate------\n",id_tutor);
            pthread_exit(NULL);
        }
        
        studentTutoredTimes=help_num-1;
        studentSequence=student_num*help_num+1;
        id_student=-1;
        
        //wait coordinator's notification
        sem_wait(&sem_coordinator);
        
        pthread_mutex_lock(&seatLock);
        //find student with highest priority from priority queue;
        //if students with same tutored times, who come first has higher priority
        int i;
        for(i=0;i<student_num;i++){
            if(priorityQueue[i][0]>-1 && priorityQueue[i][0]<=studentTutoredTimes
               && priorityQueue[i][1]<studentSequence){
                studentTutoredTimes=priorityQueue[i][0];
                studentSequence=priorityQueue[i][1];
                id_student=student_ids[i];
            }
        }
        
        //in case no student in the queue.
        if(id_student==-1) {
            pthread_mutex_unlock(&seatLock);
            continue;
        }
        
        //pop the student(reset the priority queue)
        priorityQueue[id_student-1][0]=-1;
        priorityQueue[id_student-1][1]=-1;
        
        //occupied chair--
        occupied_chair_num--;
        //all the students who are receiving tutoring now, since each tutor time slice is very tiny, so it's common that the tutoringNow is 0.
        tutoringNow++;
        pthread_mutex_unlock(&seatLock);
        
        //tutoring
        // sleeping for a random amount of time up to 0.2 ms
        float tutorTime=(float)(rand()%200)/1000;
        usleep(tutorTime);
        
        //after tutoring
        pthread_mutex_lock(&seatLock);
        //need to do tutoringNow-- after tutoring.
        tutoringNow--;
        totalSessions++;
        printf("Tu: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d\n",id_student,id_tutor-student_num,tutoringNow,totalSessions);
        pthread_mutex_unlock(&seatLock);

        //update shared data so student can know who tutored him.
        pthread_mutex_lock(&tutorFinishedQueueLock);
        tutorFinishedQueue[id_student-1]=id_tutor;
        pthread_mutex_unlock(&tutorFinishedQueueLock);
        
        //wake up the tutored student.
        //sem_post(&sem_tutorToStudent[id_student-1]);
    }
}

//coordinator action.
//1.wait student's nofitication;
//2.push student into the queue with priority;
//3.notify tutor
void *coordinator_thread(){
    while(1){
        //if all students finish, tutors threads and coordinate thread terminate.
        if(done==student_num){
            //terminate tutors first
            int i;
            for(i=0;i<tutor_num;i++){
	        //notify tutors to terminate
	        sem_post(&sem_coordinator);
            }
            
            //terminate coordinate itself
            //printf("------coordinator terminate------\n");
            pthread_exit(NULL);
        }
        
        //wait student's notification
        sem_wait(&sem_student);
        
        pthread_mutex_lock(&seatLock);
        //find the students who just seated and push them into the priority queue
        int i;
        for(i=0;i<student_num;i++){
            if(newArrivedStudentQueue[i]>-1){
                priorityQueue[i][0]=student_priority[i];
                priorityQueue[i][1]=newArrivedStudentQueue[i];
                
                printf("Co: Student %d with priority %d in the queue. Waiting students now = %d. Total requests = %d\n",student_ids[i],student_priority[i],occupied_chair_num,totalRequests);
                newArrivedStudentQueue[i]=-1;
                
                //notify tutor that student is in the queue.
                sem_post(&sem_coordinator);
            }
        }
        pthread_mutex_unlock(&seatLock);
    }
}

