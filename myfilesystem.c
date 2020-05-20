#ifndef MYFILESYSTEM_H
#define MYFILESYSTEM_H
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct initial_struct {
    char* file_data;
    char* direct_table;
    char* hash_data;
    char filename[64];
    unsigned int offset;
    unsigned int length;
    int distance;
    int nodes_at_bottom;
    int height;
    int total_nodes;
    int iterations;
    int long size_of_directory;
    int long size_of_filedata;
    int long size_of_hashdata;
    unsigned int items_copied;
    unsigned int nulls_copied;
    unsigned int next_space_after_repack;
    unsigned int total_space_availible;
} initial_struct;

typedef struct directory_block{
    char filename[64];
    unsigned int offset;
    unsigned int length;
    unsigned int distance;
} directory_block;

//the fletcher hash function inspired by psuedo code provided in project description
void fletcher(uint8_t * buf, size_t length, uint8_t * output){
    uint64_t a = 0; 
    uint64_t b = 0;
    uint64_t c = 0;
    uint64_t d = 0;
    uint32_t* data = (uint32_t*) buf;

    for (int i = 0; i < length/sizeof(uint32_t);i++){
        a = (a + data[i]) % (uint64_t)((pow(2,32) - 1));
        b = (b + a) % (uint64_t)((pow(2,32)-1));
        c = (c + b) % (uint64_t)((pow(2,32)-1));
        d = (d + c) % (uint64_t)((pow(2,32)-1));
    }
    
    uint32_t A = (uint32_t) a;
    uint32_t B = (uint32_t) b;
    uint32_t C = (uint32_t) c;
    uint32_t D = (uint32_t) d;
    
    memcpy(output, &A, sizeof(uint32_t));
    memcpy(output+4, &B, sizeof(uint32_t));
    memcpy(output+8, &C, sizeof(uint32_t));
    memcpy(output+12, &D, sizeof(uint32_t));

}

/* This is a recursive function that corrects all hashes affected by a filedata block changing. The function works by 
hashing the current node and the adjacent sibling hash together and writing the new hash to the parent index of hashdata.
Once the parent has been corrected the parent node is passed into the function which then repeats the cycle till the root has
corrected  */
void hash_block(char* current_hash,long int index,int level,char* hashdata){
    char sibling_hash[16];
    char concatenated_hash[32];
    long int parent_index = (index-1)/2;
    long int sibling_index = 0;
    //base case
    if(level == 0){
        return;
    }
    
    //check if current node if left or right
    if((index % 2) == 0){
        //current node is right child
        sibling_index = 2*(parent_index)+1; // sibling is left child
        memcpy(sibling_hash,hashdata+(16*sibling_index),16);
        memcpy(concatenated_hash,sibling_hash,16);
        memcpy(concatenated_hash+16,current_hash,16);
    } else if((index % 2) > 0){
        //current node is left child    
        sibling_index = 2*(parent_index)+2;
        memcpy(sibling_hash,hashdata+(16*sibling_index),16);
        memcpy(concatenated_hash,current_hash,16);
        memcpy(concatenated_hash+16,sibling_hash,16);
    }
    char new_hash[16];
    fletcher((uint8_t*)concatenated_hash,sizeof(concatenated_hash),(uint8_t*)new_hash);
    memcpy(hashdata+(16*parent_index),new_hash,16);
    hash_block(new_hash,parent_index,level-1,hashdata);

}

/* if only 1 block in filedata has been edited this function will write the new hash of block to hasdata in the respective
index of hashdata. All ancestral hashes are corrected using hash_block recursive function */
void compute_hash_block(size_t block_offset, void * helper){
    initial_struct* helper_data = (initial_struct*) helper;
    int hash = open(helper_data->hash_data,O_RDWR);
    int file = open(helper_data->file_data,O_RDWR);
    char* hashdata = mmap(NULL, helper_data->size_of_hashdata,PROT_READ | PROT_WRITE,MAP_SHARED,hash,0);
    char* filedata = mmap(NULL, helper_data->size_of_filedata,PROT_READ | PROT_WRITE,MAP_SHARED,file,0);
    char data[256];
    memcpy(data,filedata+(256*block_offset),256);
    char new_hash[16];
    fletcher((uint8_t*)data,256,(uint8_t*)new_hash);
    long int index_first_bottom_block = pow(2,helper_data->height) - 1; // this is the index of the first block of filedata in the binary tree hashdata i.e. the leaf on the far left
    long int index_of_block = index_first_bottom_block + block_offset; // index of the block in the hash tree array
    memcpy(hashdata+(index_of_block*16),new_hash,16);
    hash_block(new_hash,index_of_block,helper_data->height, hashdata);
    close(file);
    close(hash);
}

/* recusive function that computes all hashes in the tree once the leaves (bottom level) have been calculated in compute_hash_tree  */
void hash_tree(char* hashdata, int level){
    if(level == 0){
        return;
    }
    char child_hash_one[32];
    char child_hash_two[16];
    char new_hash[16];
    int parent_index = 0;
    for(int index = pow(2,level) -1; index <= pow(2, level+1) - 2 ; index += 2 ){ // iterates through all the nodes at each level and writes computed hash to parent node still O(n)
        memcpy(child_hash_one, hashdata + (16*index), 16 );
        memcpy(child_hash_two, hashdata + (16*(index+1)), 16 );
        memcpy(child_hash_one+16, child_hash_two, 16);       
        fletcher((uint8_t*)child_hash_one,32,(uint8_t*)new_hash);
        parent_index = (index-1)/2;
        memcpy(hashdata+(parent_index*16),new_hash,sizeof(new_hash));
    }
    hash_tree(hashdata, level-1);
}

/* computes hash tree. This function initially computes the hash of each leaf and writes it to hash data in the respectie index. Once
all the leaf hashes are updated in hashdata hash_tree is called which is a recursive funtion which then computes all nodes up to the
root hash*/
void compute_hash_tree(void * helper){
    
    initial_struct* helper_data = (initial_struct*) helper;
    int file = open(helper_data->file_data, O_RDWR);
    int hash = open(helper_data->hash_data, O_RDWR);
    char* filedata = mmap(NULL, helper_data->size_of_filedata, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
    char* hashdata = mmap(NULL, helper_data->size_of_hashdata, PROT_READ | PROT_WRITE, MAP_SHARED, hash, 0);
    char data[256];
    int index_basenode = pow(2,helper_data->height) -1; // starts with value of lowest base node
    uint8_t hashcode[16];
    for(int i = 0; i < helper_data->size_of_filedata; i+=256){
        memcpy( data, filedata+i, 256);
        fletcher((uint8_t*) data, 256, hashcode);       
        memcpy(hashdata+(index_basenode*16),hashcode, 16);
        index_basenode++;
    }
    hash_tree(hashdata, helper_data->height);
    close(file);
    close(hash);
}

/* function updates the hashdata by computing each hash block effected. compute hash block is called if
the block is effected and it is an iterative function that corrects the entire tree. If the operational cost of
computing each hash is greater than the cost to compute the entire tree then compute_hash_tree is called */
void update_hashdata(size_t changed_bytes,size_t offset, int height, void* helper){
    initial_struct* helper_data = (initial_struct*) helper;
    
    size_t distance_from_block = offset % 256; // the number of bytes from the beginning of the nearest block in filedata
    size_t first_block = (offset - distance_from_block)/256; // the index of the block in filedata
    size_t last_block = (offset + changed_bytes)/256;
    size_t blocks_changed = last_block - first_block + 1;
    size_t cost_compute_block = (  (log(helper_data->total_nodes + 1)) / (log(2))  )*blocks_changed ;  // the number of operations by computing each changed block
    size_t cost_compute_hashtree = helper_data->total_nodes; //cost to compute entire hashtree

    // printf("first index %ld\n",first_block);
    // printf("last index %ld\n",last_block);

    if(cost_compute_block < cost_compute_hashtree){
        //compute each block
        for(int i = first_block; i <= last_block;i++){
            compute_hash_block(i, (void*) helper_data);
        }
        return;
    } else {
        compute_hash_tree((void*) helper_data);
    }


}


/* searches directory table for filename specified. Once filename found in directory it
stores the filename,offset,length in the helper data.If file found return 0 for success and 
if it cannot find the file it returns 1 for failiure */
int block_search(char* filename,void* helper){
    
    initial_struct* helper_data = (initial_struct*) helper;
    FILE* directory = fopen(helper_data->direct_table,"rb");
    if(directory == NULL){
        printf("did not open file\n");
    } 
    //loop through directory table checking each 72 byte block                                
    int long size = helper_data->size_of_directory;    
    for(int i = 0; i < size/72; i++){
        fseek(directory,i*72,SEEK_SET);
        fread(helper_data->filename,sizeof(char),64,directory);
        if(strcmp(helper_data->filename,filename) == 0){
            fread(&(helper_data->offset),sizeof(int),1,directory);
            fread(&(helper_data->length),sizeof(int),1,directory);
            helper_data->distance = i*72;
            return 0;
        }
    }
    fclose(directory);
    return -1;

}

//compare function for qsort sorts array of files by smallest offset
int compare(void* block, void* block_two){

    directory_block* block_A = (directory_block*) block;
    directory_block* block_B = (directory_block*) block_two;
    return (block_A->offset - block_B->offset);

}

/* Creates an array of of directory table blocks that only contain data.
It exculdes emtpty blocks or where blocks have been deleted. This function returns a pointer
to the array of directory table blocks. This space must be freed by the caller of function. The array is not sorted */
directory_block* array_of_directory_blocks(void* helper){

    initial_struct* helper_data = (initial_struct*) helper;
    FILE* directory = fopen(helper_data->direct_table,"r+b");
    directory_block* array = malloc( (helper_data->size_of_directory/72)*sizeof(directory_block) ); // malloc space needed for maximum size (i.e. worst case)
    char null_byte[64] = {0};
    directory_block* block = malloc(sizeof(directory_block));   
    helper_data->items_copied = 0;
    for(int i=0;i<helper_data->size_of_directory/72;i++){
        fseek(directory,i*72,SEEK_SET);
        fread(block->filename,sizeof(char),64,directory);
        if(strcmp(block->filename,&null_byte[0]) != 0){         // compares if current filename is not null
            fread(&(block->offset),sizeof(int),1,directory);
            fread(&(block->length),sizeof(int),1,directory);
            helper_data->items_copied++;
            block->distance = i*72;
            array[helper_data->items_copied-1] = *block;
            
        }        
    }
    free(block);
    fclose(directory);   
    return array;

}

//malloc space for myfilesystem to use throughout program
void* init_fs(char * f1, char * f2, char * f3, int n_processors){

    initial_struct* helper_data = malloc(sizeof(initial_struct)); //malloc space for helper  
    helper_data->file_data = f1;        //store filenames arguments f1,f2 and f3 in helper for later use
    helper_data->direct_table = f2;
    helper_data->hash_data = f3;
    FILE* directory = fopen(helper_data->direct_table,"r+b");
    FILE* file_data = fopen(helper_data->file_data,"r+b");    
    if(directory == NULL){
        printf("did not open file\n");
        return NULL;
    } 
    if(file_data == NULL){
        printf("did not open file\n");
        return NULL;
    } 
    
    struct stat dt;                             //find size of of both files and store them in the helper folder
    struct stat fd;
    struct stat hd;
    int long size;

    if(stat(helper_data->hash_data,&hd)==0){
        size = hd.st_size;
        helper_data->size_of_hashdata = size;
    }else{
        perror("could not compute file size");
    }

    if(stat(helper_data->direct_table,&dt)==0){
        size = dt.st_size;
        helper_data->size_of_directory = size;
    }else{
        perror("could not compute file size");
    }

    if(stat(helper_data->file_data,&fd)==0){
        size = fd.st_size;
        helper_data->size_of_filedata = size;
    }else{
        perror("could not compute file size");
    }

    helper_data->nodes_at_bottom = helper_data->size_of_filedata/256;
    helper_data->height = ( log(helper_data->nodes_at_bottom)/log(2) );
    helper_data->total_nodes = pow(2,helper_data->height+1) - 1;
    
    fclose(directory);
    fclose(file_data);
    return (void*) helper_data;

}

void close_fs(void * helper){
    free(helper);
}

void repack(void * helper){

    initial_struct* helper_data = (initial_struct*) helper;
    int long size;
    size = helper_data->size_of_filedata;                           //size of filedata    
    directory_block* ptr = array_of_directory_blocks((void*) helper_data);
    directory_block array[helper_data->items_copied];              // array of directory blocks that are not null & size of items copied
    memcpy(array,ptr,sizeof(directory_block)*helper_data->items_copied); // copy to new array which only includes non null blocks
    free(ptr);                                                     //free pointer returned from array_of_directory_blocks()
    qsort(array,helper_data->items_copied,sizeof(directory_block), (void*) compare); //sort blocks by offset
    FILE* filedata = fopen(helper_data->file_data,"r+b");
    FILE* directory_table = fopen(helper_data->direct_table,"r+b"); 
    int next_space_availible = 0;                                  //next_space_availible is effectively a curser of the repacked file data   
    for(int x = 0; x < helper_data->items_copied; x++){
        if(next_space_availible < array[x].offset){
            //rewrite data in file data
            fseek(filedata,array[x].offset,SEEK_SET);
            char data[array[x].length];
            fread(data, sizeof(char), array[x].length, filedata);
            fseek(filedata,next_space_availible,SEEK_SET);
            fwrite(data,sizeof(char), array[x].length, filedata);
            //rewrite offset in directory
            fseek(directory_table,array[x].distance + 64, SEEK_SET);
            fwrite(&next_space_availible,sizeof(int),1,directory_table);
        }
        next_space_availible = next_space_availible + array[x].length;
    }
    helper_data->next_space_after_repack = next_space_availible;
    helper_data->total_space_availible = size - next_space_availible;
    compute_hash_tree((void*)helper_data);
    fclose(filedata);
    fclose(directory_table);
}

//creates file in next availible space
int create_file(char * filename, size_t length, void * helper){

    initial_struct* helper_data = (initial_struct*) helper;
    if(block_search(filename,(void*) helper_data) != -1){
        return 1;
    } 
    FILE* directory = fopen(helper_data->direct_table,"r+b");
    FILE* file_data = fopen(helper_data->file_data,"r+b");
    directory_block* ptr = array_of_directory_blocks((void*) helper_data); //creates an array of files 
    directory_block array[helper_data->items_copied];
    memcpy(array,ptr,sizeof(directory_block)*helper_data->items_copied); 
    free(ptr);
    qsort(array,helper_data->items_copied,sizeof(directory_block), (void*) compare); // sorts the files in order of smallest offest
    
    char null_byte = '\0'; //written to filedata were file is created
    char *point = &null_byte;
    int was_wrriten = 0; //boolen variable used to determine if a file was written to filedata
    int size_of_array = helper_data->items_copied; //number of items in the array
    int next_write_spot = 0; //index of next possible space to start writing set to 0 as the function will attempt to write at 0 if no file is presant
    int space_to_write = 0;
    int i; // number of iterations used to see if entire array was iterated
    
    // loop determines if there is enough space for new file even after filedata is repacked    
    int space_in_disk = 0;
    for(int y=0; y < size_of_array;y++){                          
        space_in_disk = space_in_disk + array[y].length;
    }    
    space_in_disk = helper_data->size_of_filedata - space_in_disk;
    if(space_in_disk < length){
        return 2;
    }    

    //if filedata and directory are empty
    if(helper_data->items_copied == 0 && length < helper_data->size_of_filedata){
        for(int x = 0; x < length; x++){
            fwrite(point,sizeof(char),1,file_data);
        }  
        was_wrriten = 1;
    }

    //creates file in next contigious space
    if(was_wrriten == 0){
        for(i=0;i<size_of_array;i++){
            space_to_write = array[i].offset - next_write_spot; //determines the size of the next availible contiguous spot

            //checks if the length of new file will fit in next space to write
            if(space_to_write > length){
                fseek(file_data,next_write_spot,SEEK_SET);
                for(int x = 0; x < length; x++){
                    fwrite(point,sizeof(char),1,file_data);
                }                    
                was_wrriten = 1;
                break;          
            }
            //if statement for last item in array must compare the space between end of file and next availible space
            if (i == size_of_array -1) {
                space_to_write =  helper_data->size_of_filedata - array[i].offset - array[i].length;
                if(space_to_write > length){
                next_write_spot = array[i].offset + array[i].length;
                fseek(file_data,next_write_spot,SEEK_SET);
                for(int x = 0; x < length; x++){
                    fwrite(point,sizeof(char),1,file_data);
                }                    
                was_wrriten = 1;
                break;          
                }
            }
            next_write_spot = array[i].offset + array[i].length;  //increase next spot availible to write to where the current file finishes
        }
    }    

    //repack needed to write if no contigous space was found
    if(was_wrriten == 0 && i==size_of_array){  
        repack(helper_data);
        next_write_spot = helper_data->next_space_after_repack;
        if(helper_data->size_of_filedata - next_write_spot > length){
            fseek(file_data,next_write_spot,SEEK_SET);
            for(int x = 0; x < length; x++){
                fwrite(point,sizeof(char),1,file_data);
            }      
            was_wrriten = 1;
        } else {
            fclose(directory);
            fclose(file_data);
            return 2;
        }
    }   
    int len = (int) length;

    //write new file into directory if written
    if(was_wrriten == 1){         
        block_search(point,(void*) helper_data);    // find next space in directory table by search the first null byte to write new information  
        fseek(directory,helper_data->distance,SEEK_SET); //seek to next availible space 
        fwrite(filename,sizeof(char),strlen(filename),directory); 
        fwrite(point,sizeof(char),1,directory);                 
        fseek(directory,helper_data->distance+64,SEEK_SET);
        fwrite(&next_write_spot,sizeof(int),1,directory);
        fwrite(&len,sizeof(int),1,directory);
        update_hashdata(length,next_write_spot,helper_data->height,(void*)helper_data); // update hash after editing file data
        fclose(directory);
        fclose(file_data);    
        return 0;
    }      
    
    fclose(directory);
    fclose(file_data); 
    return 1;

}

int delete_file(char * filename, void * helper){
    if(block_search(filename, helper) == -1){
        return 1;        
    }
    initial_struct* helper_data = (initial_struct*) helper;
    FILE* directory = fopen(helper_data->direct_table,"r+b");
    if(directory == NULL){
        return 1;
    }
    char num = '\0';
    char delete[72];
    for(int i = 0;i<64;i++){
        delete[i] = num;
    }
    fseek(directory,helper_data->distance,SEEK_SET);
    fwrite(delete,sizeof(delete),1,directory); 
    fclose(directory);                          
    return 0;
}

int resize_file(char * filename, size_t length, void * helper){

    initial_struct* helper_data = (initial_struct*) helper;
    if(block_search(filename, (void*) helper_data) == -1){ //finds file and stores the properties of the file from the driectory table into helper data
        printf("could not find file \n");
        return 1;
    }
    directory_block* ptr = array_of_directory_blocks((void*) helper_data); //creates an array of directory blocks that are not null
    directory_block array[helper_data->items_copied];
    memcpy(array,ptr,sizeof(directory_block)*helper_data->items_copied); 
    free(ptr);
    qsort(array,helper_data->items_copied,sizeof(directory_block), (void*) compare); //sorts the array by smallest offset
    int sizeOfarray = helper_data->items_copied;
    FILE* directory = fopen(helper_data->direct_table,"r+b");
    FILE* file_data = fopen(helper_data->file_data,"r+b");
    char* data = (char*) calloc(length,sizeof(char));
    int oldlength = helper_data->length;
    char original_data[helper_data->length];
    int i;
    int space_in_disk = 0;

    // loop determines if there is enough space for rezize after filedata has been repacked    
    for(int y=0; y < sizeOfarray;y++){                          
        if(strcmp((array[y].filename),filename) == 0){
            i = y;             
        }
        space_in_disk = space_in_disk + array[y].length;
    }    
    space_in_disk = space_in_disk - array[i].length;
    space_in_disk = helper_data->size_of_filedata - space_in_disk;
    if(space_in_disk < length){
        free(data);
        return 2;
    }

    /* if" determines if the resize is smaller(and file must be concatenated) or 
    greater than current length(and file size should be increased if there is space) */
    if(oldlength > length ){
        //resizing to smaller length
        fseek(file_data,helper_data->offset + length,SEEK_SET);
        fwrite(data,sizeof(char),1,file_data);
        fseek(directory,helper_data->distance+68,SEEK_SET);
        fwrite(&length,sizeof(int),1,directory);
        fclose(directory);
        fclose(file_data);
        update_hashdata(0,helper_data->offset + length,helper_data->height,(void*) helper_data);
        free(data);
        return 0;
    } else if(oldlength < length ){
        // resizing to a greater length need to check if repack is needed before increase size

        /* determines the index of the next item. the next_item == the start of the next folder or 
        if it's the last file in filedata the next_item is the end of the file size. next item is used to determine
        if there is space at the current position to resize before it hits the next item/file */
        int next_item;
        if(i < sizeOfarray-1){
            next_item = array[i+1].offset; 
        } else if(i == sizeOfarray-1){
            next_item = helper_data->size_of_filedata;
        }  

        //If resize cannot hapen in the next contiguous space repack and write file at the end
        if(helper_data->offset + length > next_item){
            
            //store original data            
            fseek(file_data,array[i].offset,SEEK_SET);
            fread(original_data,sizeof(char),array[i].length,file_data);

            // repack and change directory
            delete_file(filename, (void*) helper_data);
            repack((void*) helper_data);
            fseek(directory,array[i].distance,SEEK_SET);
            fwrite(array[i].filename,sizeof(char),sizeof(array[i].filename),directory);
            fwrite(data,sizeof(char),1,directory);
            fseek(directory,array[i].distance+64,SEEK_SET);
            fwrite(&(helper_data->next_space_after_repack),sizeof(int),1,directory);
            fwrite(&(length),sizeof(int),1,directory);

            //write to filedata
            fseek(file_data,helper_data->next_space_after_repack,SEEK_SET);
            fwrite(original_data,sizeof(char),array[i].length,file_data);
            fwrite(data,sizeof(char),length-array[i].length,file_data);
            fclose(directory);
            fclose(file_data);
            update_hashdata(length,helper_data->next_space_after_repack,helper_data->height,(void*) helper_data); // update hashdata after repack
            free(data);
            return 0;

        } else {   
            //write null bytes into new spaces after already existing file data         
            fseek(file_data, helper_data->offset+helper_data->length, SEEK_SET);
            fwrite(data, sizeof(char), length-helper_data->length, file_data);
            fseek(directory,helper_data->distance+68,SEEK_SET);
            fwrite(&length,sizeof(int),1,directory);
            fclose(directory);
            fclose(file_data);
            update_hashdata(length-helper_data->length,helper_data->offset+helper_data->length,helper_data->height,(void*) helper_data);
            free(data);
            return 0;
        }        
    }     
    free(data);
    return 1;

}

int rename_file(char * oldname, char * newname, void * helper){
    if(strlen(newname)>64){
        return 1;
    }
    if(block_search(newname, helper) == 0){
        return 1;        
    }
    if(block_search(oldname, helper) == -1){
        return 1;        
    }
    initial_struct* helper_data = (initial_struct*) helper;
    FILE* directory = fopen(helper_data->direct_table,"r+b");
    if(directory == NULL){
        printf("did not open file\n");
        return 1;
    }
    char delete = '\0';
    fseek(directory,helper_data->distance,SEEK_SET);
    fwrite(newname,strlen(newname),1,directory);
    fwrite(&delete,sizeof(char),1,directory); 
    fclose(directory);                          
    return 0;                 
    
}

/* verifies hashes with the hash of data from the block read. index is the index of the block in the merkle tree. This is a recursive funtion
called from verify_hashes_read which moves up the tree until the root hash has been verified */
int verify_hash(char* current_hash,long int index,int level,char* hashdata){
    char sibling_hash[16];
    char concatenated_hash[32];
    long int parent_index = (index-1)/2;
    long int sibling_index = 0;
    //base case
    if(level == 0){
        return 0;
    }
    
    //check if current node if left or right
    if((index % 2) == 0){
        //current node is right child
        sibling_index = 2*(parent_index)+1; // sibling is left child
        memcpy(sibling_hash,hashdata+(16*sibling_index),16);
        memcpy(concatenated_hash,sibling_hash,16);
        memcpy(concatenated_hash+16,current_hash,16);
    } else if((index % 2) > 0){
        //current node is left child    
        sibling_index = 2*(parent_index)+2;
        memcpy(sibling_hash,hashdata+(16*sibling_index),16);
        memcpy(concatenated_hash,current_hash,16);
        memcpy(concatenated_hash+16,sibling_hash,16);
    }
    // printf("current hash %ld hashed with sibling at %ld\n",index,sibling_index);
    char new_hash[16];
    char old_hash[16];
    fletcher((uint8_t*)concatenated_hash,sizeof(concatenated_hash),(uint8_t*)new_hash);
    memcpy(old_hash,hashdata+(16*parent_index),16);
    if( memcmp((void*)old_hash,(void*)new_hash,16) != 0){
        // printf("hashes did not match\n");
        return 1;
    }
    // printf("compared hash %ld\n",parent_index);
    return verify_hash(new_hash,parent_index,level-1,hashdata);
}

/* verifies hashes read by first calculating the range of blocks read (as indexed in the binary tree) 
    e.g. read from offset 60 to 270 will correspond to a range of 0 to 1 and for a tree with 15 nodes indexes 7 to 8. 
    The range of blocks is then passed to a recursive function verify_hash which recursively checks each block involved up
    to the root hash*/
int verify_hashes_read(size_t offset,size_t count, void* helper){

    initial_struct* helper_data = (initial_struct*) helper;
    size_t bottom_left_leaf_index = pow(2, helper_data->height) -1;
    size_t distance_from_block = (helper_data->offset+offset) % 256; // the number of bytes from the beginning of the nearest block in filedata
    size_t block_index = ((helper_data->offset+offset) - distance_from_block)/256; // the index of the block in filedata
    size_t index_in_hashtree = bottom_left_leaf_index + block_index; // the index of the first block where reading starts in the binary tree
    distance_from_block = (helper_data->offset+offset+count) % 256; // the number of bytes from the nearest block for last block read
    size_t last_block_index = ((helper_data->offset+offset+count) - distance_from_block)/256; // the index of last block read in filedata
    size_t last_index_read = bottom_left_leaf_index + last_block_index; // index in binary tree of last block read
    
    char filedata_hash[16];
    char current_hashdata[16];
    char data[256];
    int file = open(helper_data->file_data, O_RDWR);
    int hash = open(helper_data->hash_data, O_RDWR);
    char* filedata = mmap(NULL, helper_data->size_of_filedata, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
    char* hashdata = mmap(NULL, helper_data->size_of_hashdata, PROT_READ | PROT_WRITE, MAP_SHARED, hash, 0);
    int verified = -1;
    
    /* compute hash for the block read and compare it with current hash in hashdata. Then recursively move up through the binary tree
    comparing the hashdata to the hash calculated from the block read. the loop iterates through every block that has been read*/
    for(size_t i=index_in_hashtree;i<=last_index_read;i++){
        memcpy(data,filedata+(256*block_index),256);
        block_index++;
        fletcher((uint8_t*)data,256,(uint8_t*)filedata_hash); //the filedata from the block read into fletcher
        memcpy(current_hashdata,hashdata+(i*16),16);
        if(memcmp(current_hashdata,filedata_hash,16) != 0){  //checks if first hash calculated above is same as value in hashdata
            return 1;
        } else{
            verified = verify_hash(current_hashdata,i,helper_data->height,hashdata); //if first hash is the same it checks the rest of the nodes up to root
            if(verified != 0){
                return 1;
            }
        }
    }

    return 0;
}

int read_file(char * filename, size_t offset, size_t count, void * buf, void * helper){
    if(block_search(filename, helper) == -1){
        return 1;
    }
    initial_struct* helper_data = (initial_struct*) helper;
    if( (helper_data->length - offset) < count ){          // if the count is more than bytes left to write return 2
        return 2;
    }
    FILE* filedata = fopen(helper_data->file_data,"rb");
    if(filedata == NULL){
        printf("cannot open %s", helper_data->file_data);
        return -1;
    } 

    //verify data
    int verified = -1;
    verified = verify_hashes_read(offset,count, (void*) helper_data );
    if(verified != 0){
        return 3;
    }

    //if verified read the data
    fseek(filedata,offset+(helper_data->offset),SEEK_SET);
    fgets(buf,count+1,filedata);                     
    fclose(filedata);
    return 0;
}

int write_file(char * filename, size_t offset, size_t count, void * buf, void * helper){

    if(block_search(filename, helper) == -1){ //locate file and store information about the file in helper
        return 1;
    }
    initial_struct* helper_data = (initial_struct*) helper;
    if(helper_data->length < offset){
        return 2;
    }
    if(count+offset > helper_data->length){
        int x = resize_file(filename,count,(void*) helper_data);
        if(x == 2){
            printf("too big \n");
            return 3;
        } 
    }
    block_search(filename, (void*) helper_data); //after resize file information has changed block search again to store correct information
    FILE* filedata = fopen(helper_data->file_data,"r+b");
    fseek(filedata,helper_data->offset+offset,SEEK_SET);
    fwrite(buf,sizeof(char),count,filedata);
    update_hashdata(count,helper_data->offset+offset,helper_data->height,(void*)helper_data); //update hash data before 
    fclose(filedata);
    return 0;

}

ssize_t file_size(char * filename, void * helper){
    if(block_search(filename, helper) == -1){
        return -1;
    }
    initial_struct* helper_data = (initial_struct*) helper;
    return helper_data->length;
}

#endif