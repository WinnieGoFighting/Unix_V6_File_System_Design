#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <math.h>
#include <time.h>
#include <string.h>

#include <iostream>
#include <stdlib.h>
#include <stddef.h>
using namespace std;

#define BLOCK_SIZE      1024
#define MAINTAINED_NUM  120
#define ADDR_ELEMENT    9
#define max             200//command size limit

#define ALLOC_FLAG      (1 << 15)
#define DIR_FLAG        (1 << 14)
#define LARGE_FLAG      (1 << 12)
#define USER_R_FLAG     (1 << 8)
#define USER_W_FLAG     (1 << 7)
#define USER_E_FLAG     (1 << 6)
#define OTHER_R_FLAG    (1 << 2)

//SuperBlock: 984 bytes
typedef struct
{
    unsigned int isize;
    unsigned int fsize;
    unsigned int nfree;
    unsigned int free[MAINTAINED_NUM];
    unsigned int ninode;
    unsigned int inode[MAINTAINED_NUM];
    char flock;
    char ilock;
    char fmod;
    unsigned short time[2];
} SuperBlock;

//Inode: 64 bytes
typedef struct
{
    unsigned short flags;
    unsigned short nlinks;
    unsigned short uid;
    unsigned short gid;
    unsigned int size;
    unsigned int addr[ADDR_ELEMENT];
    unsigned int actime[2];
    unsigned int modtime[2];
} Inode;

typedef struct
{
    unsigned int nfree = 0;
    unsigned int free[MAINTAINED_NUM];

} HeadBlock;

//directory file: 32 bytes
typedef struct
{
    unsigned int iNum; //the dirfile's inode #
    char filename[28];
} DirFile;

//indirect block: 1024 bytes
typedef struct
{
    unsigned int addr[256];
} IndirectBlock;


#define IPB (BLOCK_SIZE / sizeof(Inode))//INODES_PER_BLOCK

//find the disk address for a block
unsigned int find_block(unsigned int n) {
    return n * BLOCK_SIZE;
}

//find the disk address for an inode
unsigned int find_inode(unsigned int n) {
    n--;
    return find_block(n / IPB + 2) + (n % IPB) * sizeof(Inode);
}

SuperBlock get_info_from_sb(int fd) {
    SuperBlock sb;
    lseek(fd,find_block(1), SEEK_SET);
    read(fd, &sb.isize, sizeof(SuperBlock));
    return sb;
}

void update_info_into_sb(int fd, SuperBlock sb) {
    lseek(fd, find_block(1), SEEK_SET);
    write(fd, &sb, sizeof(SuperBlock));
}


//check the context of the superblock.
void checkSuperBlock(int fd ){
//When a file is opened, returning fd, then use this function to check.
//This function is for testing.
    SuperBlock buf;
    int offset = BLOCK_SIZE;
    lseek(fd, offset, SEEK_SET);
    read(fd, &buf.isize, sizeof(buf));
    int i = (buf.nfree-1 >= 0)? (buf.nfree-1):0;
    int j = (buf.ninode-1 >= 0)? (buf.ninode-1):0;
    // cout << endl;
    // cout << "isize=" << buf.isize << " fsize=" << buf.fsize << " nfree=" << buf.nfree << " ninode=" << buf.ninode << endl;

    // for(int k = 0; k <= i ; k++){
    //     cout << buf.free[k] << endl;
    // }
}

//Check the context of the element in linked list.
//Testing function
void checkBlock(int fd, int n){
    HeadBlock buf;
    int offset = find_block(n);
    lseek(fd, offset, SEEK_SET );
    read(fd, &buf.nfree, sizeof(buf));
    cout << endl;
    cout << "The number of free blocks: " << buf.nfree << endl;
    for(int i = 0; i < buf.nfree; i++){
          cout << "#" << buf.free[i] << endl;
    }
}


void free_block(int fd, unsigned int n) {
     SuperBlock sb;
     sb = get_info_from_sb(fd);

    if (sb.nfree == MAINTAINED_NUM) {
        //copy nfree and the free array into the head block
        HeadBlock head;
        head.nfree = sb.nfree;
        for (int i = 0; i < MAINTAINED_NUM; i++)
            head.free[i] = sb.free[i];
        lseek(fd, find_block(n), SEEK_SET);
        write(fd, &head.nfree, sizeof(head));
        sb.nfree = 0;
    }
    sb.free[sb.nfree] = n;
    sb.nfree++;
    update_info_into_sb(fd, sb);
}

unsigned int allocate_block(int fd) {
    SuperBlock sb;
    unsigned int block = 0;
    sb = get_info_from_sb(fd);
    if (sb.nfree > 0) {
        sb.nfree--;
        block = sb.free[sb.nfree];
        if (sb.nfree == 0) {
        //read in the block and update the nfree and free[]
            HeadBlock head;
            lseek(fd, find_block(sb.free[0]), SEEK_SET);
            read(fd, &head, sizeof(head));
            sb.nfree = head.nfree;
            for (int i = 0; i < head.nfree; i++)
                sb.free[i] = head.free[i];
        }
    }
    update_info_into_sb(fd, sb);
    return block;
}


void free_inode(int fd, unsigned int n) {
    SuperBlock sb;
    sb = get_info_from_sb(fd);
    Inode inode;
//Initialize an inode, to set the flags to 0.
    inode.flags = 0;
    lseek(fd,find_inode(n), SEEK_SET);
    write(fd, &inode.flags, sizeof(Inode));

    if (sb.ninode < MAINTAINED_NUM) {
          sb.inode[sb.ninode] = n;
          sb.ninode++;
    }
    //update the superblock information
    update_info_into_sb(fd, sb);
}

unsigned int allocate_inode(int fd, int tInodes) {
    SuperBlock sb;
    sb = get_info_from_sb(fd);
    unsigned int iNum = 0;
    Inode in;
    //if ninode is 0, read the i-list and place the number of all free inodes(up to 120) into the inode array
    if (sb.ninode == 0) {
        for (int i = 2; i <= tInodes; i++) {
            if (sb.ninode == 120)
                break;
            lseek(fd, find_inode(i), SEEK_SET);
            read(fd, &in, sizeof(Inode));
            if (in.flags >= 32768)
                continue;
            else {
                sb.inode[sb.ninode] = i;
                sb.ninode++;
            }
        }
    }
    if (sb.ninode > 0) {
        sb.ninode--;
        iNum = sb.inode[sb.ninode];
        in.flags = ALLOC_FLAG;
        lseek(fd, find_inode(iNum), SEEK_SET);
        write(fd, &in, sizeof(Inode));
    }
    update_info_into_sb(fd, sb);
    return iNum;
}

void time_convertion(int a, int b){
    struct tm *tblock;
    long timer = (a << 32) + b;
    tblock = localtime(&timer);
    cout << endl;
    cout << "***********************Local time is: " << asctime(tblock) << endl;
}

unsigned int search_dir_inode(int fd, unsigned int piNum, char* dirName) {
    unsigned int iNum = 0;
    Inode pInode;
    lseek(fd, find_inode(piNum), SEEK_SET);
    read(fd, &pInode, sizeof(Inode));
    if (pInode.flags == ALLOC_FLAG + DIR_FLAG + USER_R_FLAG + USER_W_FLAG + USER_E_FLAG + OTHER_R_FLAG) {
        unsigned int logNum = (pInode.size - 1) / BLOCK_SIZE + 1; //will search in logNum blocks for dirName. For simplicity, assume logNum <=8 first.
        for (int i = 0; i < logNum; i++) {
            int block = pInode.addr[i];
            for(int j = 0; j < (BLOCK_SIZE - 1)/sizeof(DirFile) + 1; j++) {
                DirFile entry;
                lseek(fd, find_block(block) + j * sizeof(DirFile), SEEK_SET);
                read(fd, &entry, sizeof(DirFile));
                if(strcmp(entry.filename, dirName) == 0) {
                    iNum = entry.iNum;
                    return iNum;
                }
            }
        }
    }
    return iNum;
}



unsigned int find_file_tail(int fd, unsigned int iNum) {
    Inode inode;
    lseek(fd, find_inode(iNum), SEEK_SET);
    read(fd, &inode, sizeof(inode));
    unsigned int logNum = (inode.size - 1) / BLOCK_SIZE + 1;
    // addre[] points to the data block
    return find_block(inode.addr[logNum - 1]) + inode.size % BLOCK_SIZE;
}

unsigned int create_dir(int fd, unsigned int piNum, char* dirName,int tInodes) {
    unsigned int new_iNum = allocate_inode(fd, tInodes);
    unsigned int new_block = allocate_block(fd);

    //update parent_dir info
    Inode p_inode;
    DirFile new_dir;
    new_dir.iNum = new_iNum;
    strcpy(new_dir.filename, dirName);
    int offset = find_file_tail(fd, piNum);

    if (offset % BLOCK_SIZE == 0) {
        int temp = allocate_block(fd);
        offset = find_block(temp);
    }
    lseek(fd, offset, SEEK_SET);
    write(fd, &new_dir, sizeof(DirFile));

    //go to parent inode and increment its file size
    Inode inode;
    lseek(fd, find_inode(piNum), SEEK_SET);
    read(fd, &inode, sizeof(inode));
    inode.size += sizeof(DirFile);
    lseek(fd, find_inode(piNum), SEEK_SET);
    write(fd, &inode, sizeof(inode));


    //write new inode
    Inode new_inode;
    new_inode.flags = ALLOC_FLAG + DIR_FLAG + USER_R_FLAG + USER_W_FLAG + USER_E_FLAG + OTHER_R_FLAG;
    new_inode.size = sizeof(DirFile) * 2;
    new_inode.addr[0] = new_block;

    lseek(fd, find_inode(new_iNum),SEEK_SET);
    write(fd, &new_inode, sizeof(new_inode));

    //write new data block
    DirFile temp1, temp2;
    temp1.iNum = new_iNum;
    temp2.iNum = piNum;
    strcpy(temp1.filename, ".");
    strcpy(temp2.filename, "..");

    lseek(fd, find_block(new_block), SEEK_SET);
    write(fd, &temp1, sizeof(DirFile));
    write(fd, &temp2, sizeof(DirFile));

    return new_iNum;
}

unsigned int mkdir(int fd, char* dirName, int tInodes, int flag) {
    char* token = strtok(dirName, "/");
    unsigned int piNum = 1;
    while (token != NULL) {
        int iNum = search_dir_inode(fd, piNum, token);
        if (iNum == 0) {
            piNum = create_dir(fd, piNum, token, tInodes);
            token = strtok(NULL, "/");
            flag = 3;
        }
        else {
            piNum = iNum;
            token = strtok(NULL, "/");
            if (token == NULL && flag) {
            cout << "Error: fail to create directory: Already Exists! " << endl;

            }
        }
    }
    if (flag == 3) 
      cout<< "Successful! inode "<< piNum << " has been allocated to the new directory." << endl;
    return piNum;
}

string split_directory(string dirName) {
    int tail = dirName.find_last_of("/");
    dirName = dirName.substr(0, tail);
    return dirName;
}

string split_filename(string dirName) {
    int tail = dirName.find_last_of("/");
    dirName= dirName.substr(tail + 1);
    return dirName;
}

void cpin(int fd,char* external_file, char* v6_file, int tInodes) {
    //create v6_file in v6 file system
    int new_iNum = allocate_inode(fd, tInodes);
    Inode new_inode;
    new_inode.flags = ALLOC_FLAG + USER_R_FLAG + USER_W_FLAG + USER_E_FLAG + OTHER_R_FLAG;

    //open external file
    char buff[BLOCK_SIZE];
    int readSize = 0; //how many bytes read in the buff
    int num = 0; // how many block has been allocated
    int addrNum = 0; // which addr[] now
    int indirectAddr1, indirectAddr2, indirectAddr3 = 0; // which first and second level indirect block addr[] now
    int block = 0; // new block num
    int inblock = 0; // new indirect block num
    int ex_fd = open(external_file, 0); //read only
    int level1Block;
    int level2Block;
    int level3Block;

    while (true) {
        readSize = read(ex_fd, &buff, BLOCK_SIZE);
	    // cout << readSize << endl;
        if (readSize == 0){
            new_inode.size = num * BLOCK_SIZE;
            // cout<< "num is: " << num << endl;
            // cout<< "readSize is: " << readSize << endl;
            cout<< "file size is: " << new_inode.size << endl;
            break;
        }

        block = allocate_block(fd);
        // cout << "readSize is:  + 1 + 1" << readSize << endl;
        lseek(fd, find_block(block), SEEK_SET);
        write(fd, &buff, BLOCK_SIZE);
        if (num < 9){ // small file
            new_inode.addr[addrNum] = block;
            addrNum ++;
            num++;
        }
        else{ // large file
            if(num == 9){
                IndirectBlock new_indirectBlock;
                addrNum = 0;
                inblock = allocate_block(fd);
                for(int i = 0; i < 9; i++){
                    new_indirectBlock.addr[i] = new_inode.addr[i];
                }
                indirectAddr1 = 9;
                new_indirectBlock.addr[indirectAddr1] = block;
                indirectAddr1++;
                //write new_indirectBlock into disk
                lseek(fd, find_block(inblock), SEEK_SET);
                write(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                new_inode.addr[addrNum] = inblock;
                addrNum++;

                // inode flag set to large file
                new_inode.flags = ALLOC_FLAG + USER_R_FLAG + USER_W_FLAG + USER_E_FLAG + OTHER_R_FLAG +  LARGE_FLAG;
                num++;
                // cout << "num is: " << num << endl;
                // cout << "addrNum is: " << addrNum << endl;
            }
            else{
                if(num < 2048) {
                    if(indirectAddr1 < 256){
                        IndirectBlock new_indirectBlock;
                        lseek(fd, find_block(inblock), SEEK_SET);
                        read(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                        new_indirectBlock.addr[indirectAddr1] = block;
                        indirectAddr1++;
                        lseek(fd, find_block(inblock), SEEK_SET);
                        write(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                        num++;
                        // cout << "num is: " << num << endl;
                        // cout << "addrNum is: " << addrNum << endl;
                        // cout << "indirectAddr1 is: " << indirectAddr1 << endl;
                    }
                    else{
                        IndirectBlock new_indirectBlock;
                        inblock = allocate_block(fd);
                        indirectAddr1 = 0;
                        new_indirectBlock.addr[indirectAddr1] = block;
                        indirectAddr1++;
                        lseek(fd, find_block(inblock), SEEK_SET);
                        write(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                        
                        new_inode.addr[addrNum] = inblock;
                        addrNum++;
                        
                        num++;
                        // cout << "num is: " << num << endl;
                        // cout << "addrNum is: " << addrNum << endl;
                    }
                }
                else{ // extra large file

                    if(num == 2048){
                        IndirectBlock level1, level2, level3;
                        indirectAddr1 = 0;
                        indirectAddr2 = 0;
                        indirectAddr3 = 0;
                        level1Block = allocate_block(fd);
                        level2Block = allocate_block(fd);
                        level3Block = allocate_block(fd);
                        level1.addr[indirectAddr1] = level2Block;
                        level2.addr[indirectAddr2] = level3Block;
                        level3.addr[indirectAddr3] = block;
                        new_inode.addr[8] = level1Block;
                        indirectAddr1++;
                        indirectAddr2++;
                        indirectAddr3++;
                        lseek(fd, find_block(level1Block), SEEK_SET);
                        write(fd, &level1, sizeof(level1));
                        lseek(fd, find_block(level2Block), SEEK_SET);
                        write(fd, &level2, sizeof(level1));
                        lseek(fd, find_block(level3Block), SEEK_SET);
                        write(fd, &level3, sizeof(level1));
                        num++;

                    }
                    else{
                        
                        if(indirectAddr3 < 256){
                            IndirectBlock new_indirectBlock;
                            lseek(fd, find_block(level3Block), SEEK_SET);
                            read(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                            new_indirectBlock.addr[indirectAddr3] = block;
                            indirectAddr3++;
                            lseek(fd, find_block(level3Block), SEEK_SET);
                            write(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                            num++;
                            // cout << "num is: " << num << endl;
                            // cout << "addrNum is: " << addrNum << endl;
                            // cout << "indirectAddr3 is: " << indirectAddr3 << endl;
                        }
                        else{ // new level3Block
                            IndirectBlock new_indirectBlock;
                            level3Block = allocate_block(fd);
                            indirectAddr3 = 0;
                            new_indirectBlock.addr[indirectAddr3] = block;
                            indirectAddr3++;
                            lseek(fd, find_block(level3Block), SEEK_SET);
                            write(fd, &new_indirectBlock, sizeof(new_indirectBlock));
                            // finish level3, then need to connect level3 to level2

                            // connect level3Block to level2Block
                            if(indirectAddr2 < 256){ // level3 can add in level2.addr
                                IndirectBlock piLevel;
                                lseek(fd, find_block(level2Block), SEEK_SET);
                                read(fd, &piLevel, sizeof(new_indirectBlock));
                                piLevel.addr[indirectAddr2] = level3Block;
                                indirectAddr2++;
                                lseek(fd, find_block(level2Block), SEEK_SET);
                                write(fd, &piLevel, sizeof(new_indirectBlock));
                                num++;
                            }
                            else{ // need a new level2
                                if(indirectAddr1 < 256){
                                    IndirectBlock new_indirectBlock;
                                    IndirectBlock piLevel;
                                    level2Block = allocate_block(fd);
                                    indirectAddr2 = 0;
                                    new_indirectBlock.addr[indirectAddr2] = level3Block;
                                    indirectAddr2++;
                                    lseek(fd, find_block(level2Block), SEEK_SET);
                                    write(fd, &new_indirectBlock, sizeof(new_indirectBlock));

                                    // connect level2block to level1
                                    lseek(fd, find_block(level1Block), SEEK_SET);
                                    read(fd, &piLevel, sizeof(new_indirectBlock));
                                    piLevel.addr[indirectAddr1] = level2Block;
                                    indirectAddr1++;
                                    lseek(fd, find_block(level1Block), SEEK_SET);
                                    write(fd, &piLevel, sizeof(new_indirectBlock));  
                                    
                                    num++;
                                }
                                else{
                                    cout<< "File is too large" << endl;
                                }
                            }

                        }
                    }
                }
            }
        }
        if(readSize < BLOCK_SIZE){
            //calculate the file size
            new_inode.size = (num - 1) * BLOCK_SIZE + readSize;
            // cout<< "BLOCK_SIZE is: " << BLOCK_SIZE << endl;
            // // cout<< "num is: " << num << endl;
            // cout<< "readSize is: " << readSize << endl;
            // cout<< "file size is: " << new_inode.size << endl;
            break;
        }
    }
    
    //write new_inode into disk
    lseek(fd, find_inode(new_iNum), SEEK_SET);
    write(fd, &new_inode, sizeof(new_inode));

    //update parent inode and parnet directory contents
    string v6_file_s(v6_file);
    string p_dir_s = split_directory(v6_file_s);
    string f_name_s = split_filename(v6_file_s);
    //const char* p_dir = p_dir_s.c_str();
    char* p_dir = const_cast<char*>(p_dir_s.c_str());
    //const char* f_name = f_name_s.c_str();
    char* f_name = const_cast<char*>(f_name_s.c_str());
    unsigned int piNum = mkdir(fd, p_dir, tInodes, 0);



    DirFile c_dir;
    c_dir.iNum = new_iNum;
    strcpy(c_dir.filename, f_name);
    int offset = find_file_tail(fd, piNum);
    lseek(fd, offset, SEEK_SET);
    write(fd, &c_dir, sizeof(DirFile));


    Inode p_inode;
    lseek(fd, find_inode(piNum), SEEK_SET);
    read(fd, &p_inode, sizeof(Inode));
    p_inode.size += sizeof(DirFile);
    lseek(fd, find_inode(piNum),SEEK_SET);
    write(fd, &p_inode, sizeof(Inode));

    //cout<< "congratulations! successfully copied!" << endl;

}

// int level1addr(int n){

// }
// int level2addr(int n){
    
// }
// int level3addr(int n){
//     int a = n;
//     while(a / 256 == 1)
// }



void cpout(int fd, char* v6_file, char* external_file, int tInodes) {
    int ex_fd = open(external_file, 2);
    string v6_file_s(v6_file);
    string p_dir_s = split_directory(v6_file_s);
    string f_name_s = split_filename(v6_file_s);
    //const char* p_dir = p_dir_s.c_str();
    char* p_dir = const_cast<char*>(p_dir_s.c_str());
    //const char* f_name = f_name_s.c_str();
    char* f_name = const_cast<char*>(f_name_s.c_str());
    unsigned int piNum = mkdir(fd, p_dir, tInodes, 0);
    unsigned int iNum = search_dir_inode(fd, piNum, f_name);

    //read the inode information of the file
    Inode inode;
    lseek(fd, find_inode(iNum), SEEK_SET);
    read(fd, &inode, sizeof(inode));

    // read the indirect block
    IndirectBlock ib;

    //calcualte the needed number of blocks
    int n = inode.size/BLOCK_SIZE + 1;
    char buff[BLOCK_SIZE];
    cout << "# of block need to read: " << n << endl;
    cout << "filesize is : " << inode.size << endl;
    //large file or small file
    int isLarge = inode.flags & LARGE_FLAG;
    if(isLarge == 0){
        cout << "isLarge? No" << endl;
    }
    else{
        cout << "isLarge? Yes" << endl;
    }

    int a;
    if(isLarge == 0){
        for (int i = 0; i < n; i++) {
            lseek(fd, find_block(inode.addr[i]), SEEK_SET);
            //clear data block
            
            if (i == n-1) {
              a = read(fd, &buff, inode.size % BLOCK_SIZE);
              cout << "a is: " << a << endl;
            }
            else {
              a = read(fd, &buff, BLOCK_SIZE);
            }
            write(ex_fd, &buff, a);
        }
    }
    else{
        if(n <= 256*8){
            int m = (n-1) / 256; // calculate the number of addr[] 
            // cout << "m is " << m << endl;
            int reminder = (n - 1) % 256;
            // cout << "reminder is: " << reminder << endl;
            for(int j = 0; j <= m; j++){ // traverse addr[] of inode
                lseek(fd, find_block(inode.addr[j]), SEEK_SET);
                read(fd, &ib, BLOCK_SIZE);
                if(j == m){
                    for(int i = 0; i <= reminder; i++){
                        lseek(fd, find_block(ib.addr[i]), SEEK_SET);
                        if (i == n-1) {
                          a = read(fd, &buff, inode.size % BLOCK_SIZE);
                        }
                        else {
                          a = read(fd, &buff, BLOCK_SIZE);
                        }
                        write(ex_fd, &buff, a);                       
                    }
                }
                else{
                    for(int i = 0; i < 256; i++){ // traverse indirect block
                        // cout << "i is: " << i << endl;
                        // cout << "ib.addr[i] is: " << ib.addr[i] << endl;
                        lseek(fd, find_block(ib.addr[i]), SEEK_SET);
                        a = read(fd, &buff, BLOCK_SIZE);
                        // cout << "a is: " << a << endl;
                        write(ex_fd, &buff, a);
                    }
                }
            }
        }
        else { // extra large file
            int m = 8;
            int t = 0;
            // cout << "m is " << m << endl;
            for(int j = 0; j < m; j++){ // traverse addr[] of inode
                lseek(fd, find_block(inode.addr[j]), SEEK_SET);
                read(fd, &ib, BLOCK_SIZE);
                for(int i = 0; i < 256; i++){ // traverse indirect block
                    // cout << "i is: " << i << endl;
                    // cout << "ib.addr[i] is: " << ib.addr[i] << endl;
                    lseek(fd, find_block(ib.addr[i]), SEEK_SET);
                    a = read(fd, &buff, BLOCK_SIZE);
                    // if(a == 0){
                    //     cout << "a == 0" << endl;
                    //     cout << "n remains: " << n << endl;
                    //     cout << "j is : " << j << endl;
                    //     cout << "i is : " << i << endl;
                    //     cout << "t is : " << t<< endl; 
                    // }
                    // cout << "a is: " << a << endl;
                    write(ex_fd, &buff, a);
                    t++;
                }
            }
            
            // cout << "t is: " << t << endl;
            // begin the addr[9] with 3 level 
            // n = n - 256*8;
            // l3 = n / 256; // level2 addr
            // r3 = n % 256;
            // l2 = l3 / 256;
            // l1 = l2 / 256;
            // for(){
            // cout << "n is : " << n << endl;
            n = n - 256*8; //block remains
            // cout << "n - 256*8 is : " << n << endl;
            int level1Block = 0;
            int level2Block = 0;
            int level3Block = 0;
            IndirectBlock indirectblock1; // level1
            IndirectBlock indirectblock2; // level2
            IndirectBlock indirectblock3; // level3
            lseek(fd, find_block(inode.addr[8]), SEEK_SET);
            read(fd, &indirectblock1, BLOCK_SIZE);
            lseek(fd, find_block(indirectblock1.addr[level1Block]), SEEK_SET);
            read(fd, &indirectblock2, BLOCK_SIZE);
            lseek(fd, find_block(indirectblock2.addr[level2Block]), SEEK_SET);
            read(fd, &indirectblock3, BLOCK_SIZE);
            level2Block++;
            level1Block++;

            for(int i = 0; i < n; i++){
                lseek(fd, find_block(indirectblock3.addr[level3Block]), SEEK_SET);
                if (i == n-1) {
                  a = read(fd, &buff, inode.size % BLOCK_SIZE);
                }
                else {
                  a = read(fd, &buff, BLOCK_SIZE);
                }
                
                // if(a == 0){
                //     cout << "a == 0" << endl;
                //     cout << "n remains: " << n << endl;
                //     cout << "level1 is : " << level1Block << endl;
                //     cout << "level2 is : " << level2Block << endl;
                //     cout << "level3 is : " << level3Block << endl;       
                //     cout << "t is : " << t<< endl; 
                // }
                // cout << "a is: " << a << endl;
                write(ex_fd, &buff, a);
                t++;
                level3Block++;
                if(level3Block == 256){
                    
                    lseek(fd, find_block(indirectblock2.addr[level2Block]), SEEK_SET);
                    read(fd, &indirectblock3, BLOCK_SIZE);
                    level3Block = 0;
                    level2Block++;
                    if(level2Block == 256){
                        lseek(fd, find_block(indirectblock1.addr[level1Block]), SEEK_SET);
                        read(fd, &indirectblock2, BLOCK_SIZE);
                        level1Block++;
                        level2Block = 0;

                        // lseek(fd, find_block(indirectblock2.addr[level2Block]), SEEK_SET);
                        // read(fd, &indirectblock3, BLOCK_SIZE);
                        // level2Block++;
                        // level3Block = 0;

                    }
                }
                // n--;
                if(i == n-1){
                    // cout << "n should be: " << inode.size/BLOCK_SIZE << endl;
                    // cout << "n remains: " << n << endl;
                    // cout << "level1 is : " << level1Block << endl;
                    // cout << "level2 is : " << level2Block << endl;
                    // cout << "level3 is : " << level3Block << endl;       
                    // cout << "t is : " << t<< endl;             
                }

            }

        }

    }

}

//
void rm(int fd, char* v6_file, int tInodes) {
    string v6_file_s(v6_file);
    string p_dir_s = split_directory(v6_file_s);
    string f_name_s = split_filename(v6_file_s);
    //const char* p_dir = p_dir_s.c_str();
    char* p_dir = const_cast<char*>(p_dir_s.c_str());
    //const char* f_name = f_name_s.c_str();
    char* f_name = const_cast<char*>(f_name_s.c_str());
    unsigned int piNum = mkdir(fd, p_dir, tInodes, 0);
    //786768676846375684673586
    unsigned int iNum = search_dir_inode(fd, piNum, f_name);
    
    if (iNum == 0) {
        cout << "ERROR: file doesn't exit." << endl;
    }
    else {
        Inode pInode;
        lseek(fd, find_inode(piNum), SEEK_SET);
        read(fd, &pInode, sizeof(Inode));
        unsigned int logNum = (pInode.size - 1) / BLOCK_SIZE + 1;
        for (int i = 0; i< logNum; i++) {
            int block = pInode.addr[i];
            for (int j = 0; j < (BLOCK_SIZE - 1)/sizeof(DirFile) + 1; j++) {
                DirFile targetFile_dir;
                int offset1 = find_block(block) + j * sizeof(DirFile);
                lseek(fd, offset1, SEEK_SET);
                read(fd, &targetFile_dir, sizeof(DirFile));
                if(strcmp(targetFile_dir.filename, f_name) == 0) {
                    DirFile tailFile_dir;
                    int offset2 = find_file_tail(fd, piNum);
                    lseek(fd, offset2, SEEK_SET);
                    read(fd, &tailFile_dir, sizeof(DirFile)); 
                    targetFile_dir.iNum = tailFile_dir.iNum;
                    strcpy(targetFile_dir.filename, tailFile_dir.filename);

                   //update directory (raplce the file_to_remove dir with the tail_file dir)
                    lseek(fd, offset1, SEEK_SET);
                    write(fd, &targetFile_dir, sizeof(DirFile));
                    cout<< "delete completed: iNum: " << iNum << " has been freed"<< endl;
                    break;
                }
            }
        }

        // update parent file size
        pInode.size -= sizeof(DirFile);
        lseek(fd, find_inode(piNum),SEEK_SET);
        write(fd, &pInode, sizeof(Inode));

        //free data blocks associated with the file
        Inode cInode;
        lseek(fd, find_inode(iNum), SEEK_SET);
        read(fd, &cInode, sizeof(Inode));
        int blks_to_free = (cInode.size - 1) /  BLOCK_SIZE + 1;
        for (int i = 0; i < blks_to_free; i++)
            free_block(fd, cInode.addr[i]);

        //free inode
        free_inode(fd, iNum);

    }

}


/*
Function to initialize the file system.
Input:
      virDisk, the name of the file to be operated on.
      tBlocks, the total number of blocks in the disk(fsize).
      tInodes, the total number of i-nodes in the disk.
output: void
Opertations:
            1. Allocate a data block for the root directory.
            2. Write the i-node #1, for the root directory.
            3. Add all other data blocks to the free list(create the linked list for free blocks).
            4. Assign default null(or 0) for all other i-nodes.
            5. Write the superblock.
*/
int initfs(char* virDisk, int tBlocks, int tInodes) {

    cout<<"Filename is:"<<virDisk<<endl;
    cout << endl;
    cout<<"Number of blocks is:"<<tBlocks<<endl;
    cout << endl;
    cout<<"Number of i-node is:"<<tInodes<<endl;

    int offset, i, j;
    int iBlocks = (tInodes -1)/ IPB + 1; //the number of blocks assigned to inodes.
    int dBlocks = tBlocks - iBlocks - 1 - 1;  //the number of datablocks.
    time_t now;
    time(&now);
    int fd = open(virDisk,2);
    
    lseek(fd,0,SEEK_SET);
    for(int i = 0; i<tBlocks; i++){
      char empty[1024] = "";
      write(fd,&empty,1024);
    }
/*********Check whether a file exists, if not ask user to create one then try to initialize again.*********/
    if (fd > 2){// Open a file correctly
          cout << endl;
          cout<<"File: "<<virDisk<<" exists"<<endl;
    }
    else{ //Need to be sure
          cout << endl;
          cout<<"File: "<<virDisk<<" doesn't exist, please create it (using touch command: touch sample.txt )then try again."<<endl;
          exit(1);
    }

//Write inode #1
    Inode inode1;
    inode1.flags  = ALLOC_FLAG + DIR_FLAG + USER_R_FLAG + USER_W_FLAG + USER_E_FLAG + OTHER_R_FLAG;
    inode1.size = sizeof(DirFile) * 2;
    int block = allocate_block(fd);
    inode1.addr[0] = block;
    inode1.actime[0] = (int)(now >> 32);
    inode1.actime[1] = (int)now;
//unsigned short modtime[2];
    lseek(fd, find_inode(1), SEEK_SET);
    write(fd, &inode1, sizeof(Inode));


//Write root directory
    DirFile root, rootParent;
    rootParent.iNum = root.iNum = 1;
    strcpy(root.filename, ".");
    strcpy(rootParent.filename, "..");

    lseek(fd, find_block(block), SEEK_SET);
    write(fd, &root, sizeof(DirFile));
    write(fd, &rootParent, sizeof(DirFile));

//Initialize super block
    SuperBlock sb;
    sb.isize = iBlocks;
    sb.fsize = tBlocks;
    sb.nfree = 0;
    sb.free[MAINTAINED_NUM];
    sb.free[0] = 0;
    sb.ninode = 0;
    sb.inode[MAINTAINED_NUM];
//Write sb into disk
    offset = BLOCK_SIZE; //skip the first block
    lseek(fd, offset, SEEK_SET);
    write(fd, &sb.isize, sizeof(sb));

//Add all other data blocks to the free list.
    for(i = iBlocks + 2 + 1; i < tBlocks; i++){
        free_block(fd,i);
    }

//free all other inodes and only record 120 free inodes in superblock inode list.
    for(i = 2; i<= tInodes; i++){
        free_inode(fd,i);
    }


/***************CHECK WHETHER INITIALIZATION IS SUCCESSFUL*****************/
/*Part1: Print all free blocks' numbers.
/*Part2: Check context of the superblock.
/*Part3: Print the context in inode #1
/*Part4: Check the root dir is written successfully.
*/

//Check the context of the element in linked list.(Free-chain-block)////For testing
//Print all free blocks by group.
    // for(i = iBlocks + 2 + MAINTAINED_NUM + 1; i <= tBlocks; i = i + MAINTAINED_NUM){
    // cout << endl;
    // cout << "The " << (i - (iBlocks + 2)) / MAINTAINED_NUM << "th 120 blocks are:" << endl;
    // checkBlock(fd, i);
    // }

///Superblock context///For testing
    checkSuperBlock(fd);

//Check the inode #1 is written successfully
    // Inode buf;
    // lseek(fd, find_inode(1), SEEK_SET);
    // read(fd, &buf, sizeof(Inode));
    // time_convertion(buf.actime[0],buf.actime[1]);
    // cout << endl;
    // cout << buf.flags << " " << buf.size << " " << buf.addr[0] << endl;

//Check the root dir is written successfully
    // DirFile x;
    // lseek(fd, find_block(block), SEEK_SET);
    // read(fd, &x.iNum, sizeof(DirFile));
    // cout << endl;
    // cout<<"root is "<< x.iNum<<" "<< x.filename << endl;
    // read(fd, &x.iNum, sizeof(DirFile));
    // cout << endl;
    // cout<<"parentroot is "<< x.iNum << " " << x.filename << endl;

//check if the block alloctaion is done properly
  //try to allocate 130 blocks
  // for (int i = 0; i < 310; i++) {
  // unsigned block = allocate_block(fd);
  // cout << "block #" << block << " has been allocated successfully." << endl;
  // }

//check if the inode allocation is done properly
//try to allocate 130 inodes
  // for (int i = 0; i < 130; i++) {
  // unsigned int node = allocate_inode(fd, tInodes);
  // cout << "inumber #" << node << " has been allocated successfully." << endl;
  // }

/*************************Test end*******************************************/
  return fd;
}


int main(int argc, char *argv[]) {

    int fd;
    int tInodes;
    long int tBlocks;
    
    while(1) {

    char command[max];
    int i = 0;
    cout <<endl;
    cout << "Pleanse input command:" << endl;

//User input command.
    while(1){
    int c = getchar();
    if(c == '\n') {
        command[i] = 0;
        break;
    }
    command[i] = c;
    if (i == max - 1) {
            cout << endl;
                        cout << "Command is too long\n" << endl;
                        exit(1);}
    i++;
    }

//Split command and argument
    char* cmd;
    char* arg;
    cmd = strtok(command," ");
    if(cmd != NULL){
        arg = strtok(NULL, "\n");//The first word is command, what's left is the argument.
    }
    cout << endl;
      
    if(strcmp(command,"initfs") == 0){
          char* p;

          //Split the arguments for initfs function: filename, tBlocks, tInodes.
          p = strtok(arg, " ");
          char* filename = p;
          p = strtok(NULL, " "); 
          tBlocks = atoi(p);//fsize
          p = strtok(NULL, " ");
          tInodes = atoi(p);//total number of i-nodes

        fd = initfs(filename, tBlocks, tInodes );
        
        cout << endl;
        cout<<"File system initialization is done."<<endl;
    }

    if(strcmp(command, "mkdir") == 0) {//Input command is to make a directory.
          char* p;
          p = strtok(arg, " ");
          char* dirName = p;
          mkdir(fd, dirName, tInodes, 1);
    }
    
    if (strcmp(command, "rm") == 0) {
          char* p;
          p = strtok(arg, " ");
          char* filename = p;
          rm(fd, filename, tInodes);
    }
    
    if (strcmp(command, "cpin") == 0) {//cpin exfile_cpin.txt v6_file
          char* p;
          p = strtok(arg, " ");
          char* externalfile = p;
          p = strtok(NULL, " ");
          char* v6_file = p;
          
          cpin(fd, externalfile, v6_file, tInodes);
    }
    
    if (strcmp(command, "cpout") == 0) {//cpout v6_file exfile_cpin.txt 
          char* p;
          p = strtok(arg, " ");
          char* v6_file = p;
          p = strtok(NULL, " ");
          char* externalfile = p;          
          
          cpout(fd, v6_file, externalfile, tInodes);
    }
    
    if(strcmp(command, "q") == 0) {//Input command is quit. Quit the program by saving all work.
         cout << "Quit the program" << endl;
         exit(1);
    }

  }//while
    return 0;

}
