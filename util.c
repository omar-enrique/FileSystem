#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ext2fs/ext2_fs.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>

#include "type.h"

/**** globals defined in main.c file ****/

MINODE minode[NMINODE];
MINODE *root;
PROC   proc[NPROC], *running;
MOUNT mtable[NMTABLE];

char gpath[256];
char *name[64]; // assume at most 64 components in pathnames
int  n;

int  fd, dev;
int  nblocks, ninodes, bmap, imap, inode_start;
char line[256], cmd[32], pathname[256];
char cwd[256];


int get_block(int dev, int blk, char *buf)
{
    lseek(dev, (long)blk*BLKSIZE, 0);
    read(dev, buf, BLKSIZE);
}   

int put_block(int dev, int blk, char *buf)
{
    lseek(dev, (long)blk*BLKSIZE, 0);
    write(dev, buf, BLKSIZE);
}   

int tokenize(char *pathname)
{
    int i = 0;
    char *s = NULL;

    strcpy(gpath, pathname);

    s = strtok(gpath, "/"); // first call to strtok()
    while(s){
        name[i] = s;
        s = strtok(0, "/"); // call strtok() until it returns NULL
        i++;
    }
    n = i;
    return i;
}

// tst_bit, set_bit functions
int tst_bit(char *buf, int bit){
    return buf[bit/8] & (1 << (bit % 8));
}

int set_bit(char *buf, int bit){
    buf[bit/8] |= (1 << (bit % 8));
}

int decFreeInodes(int dev)
{
    char buf[1024];
// dec free inodes count in SUPER and GD
    get_block(dev, 1, buf);
    sp = (SUPER *)buf;
    sp->s_free_inodes_count--;
    put_block(dev, 1, buf);
    get_block(dev, 2, buf);
    gp = (GD *)buf;
    gp->bg_free_inodes_count--;
    put_block(dev, 2, buf);
}

int ialloc(int dev)
{
    int i;
    char buf[BLKSIZE];
    // use imap, ninodes in mount table of dev
    MOUNT *mp = (MOUNT *)get_mtable(dev);
    get_block(dev, mp->imap, buf);
    for (i=0; i<mp->ninodes; i++){
        if (tst_bit(buf, i)==0){
            set_bit(buf, i);
            put_block(dev, mp->imap, buf);
            // update free inode count in SUPER and GD
            decFreeInodes(dev);
            return (i+1);
        }
    }
    return 0; // out of FREE inodes
}

MINODE *iget(int dev, int ino)
{
    MINODE *mip;
    INODE *tempIP;

    int blk, offset;
    char buf[BLKSIZE];
//   // return minode pointer to loaded INODE
//   (1). Search minode[ ] for an existing entry (refCount > 0) with 
//        the needed (dev, ino):
//        if found: inc its refCount by 1;
//                  return pointer to this minode;

    for(int i = 0; i < NMINODE; i++) {
        mip = &minode[i];

        if(mip->refCount && (mip->dev == dev) && (mip->ino == ino)) {
            mip->refCount++;
            return mip;
        }
    }

//   (2). // needed entry not in memory:
//        find a FREE minode (refCount = 0); Let mip-> to this minode;
//        set its refCount = 1;
//        set its dev, ino
    for(int i = 0; i < NMINODE; i++) {
        mip = &minode[i];

        if(mip->refCount == 0) {
            mip->refCount = 1;
            break;
        }
    }
    mip->dev = dev;
    mip->ino = ino;
    mip->refCount = 1;
    mip->dirty = 0;

//   (3). load INODE of (dev, ino) into mip->INODE:
       
    // get INODE of ino a char buf[BLKSIZE]    
    blk    = (ino-1) / 8 + inode_start;
    offset = (ino-1) % 8;

    printf("iget: ino=%d blk=%d offset=%d\n", ino, blk, offset);

    get_block(dev, blk, buf);
    tempIP = (INODE *)buf + offset;
    mip->INODE = *tempIP;  // copy INODE to mp->INODE

    return mip;
}


int iput(MINODE *mip) // dispose a used minode by mip
{
    INODE *tempIP;

    int blk, offset;
    char buf[BLKSIZE];
    mip->refCount--;
    
    if (mip->refCount > 0) return;
    if (!mip->dirty)       return;
    
    // Write YOUR CODE to write mip->INODE back to disk
    blk    = (mip->ino-1) / 8 + inode_start;
    
    offset = (mip->ino-1) % 8;
    get_block(mip->dev, blk, buf);
    tempIP = (INODE *) buf + offset;
    *tempIP= mip->INODE;
    put_block(mip->dev, blk, buf);
    mip->refCount = 0;
}


// serach a DIRectory INODE for entry with a given name
int search(MINODE *mip, char *name)
{
    char *cp;
    char temp[BLKSIZE], sbuf[BLKSIZE];
    for(int i = 0; i < 12; i++) {
        if (mip->INODE.i_block[i] == 0) 
            return 0;
        get_block(mip->dev, mip->INODE.i_block[i], sbuf);

        printf("Inode\tRec_len\tName_len\tName\n");
        dp = (DIR *) sbuf;
        cp = sbuf;
        while(cp < sbuf + BLKSIZE) {
            strncpy(temp, dp->name, dp->name_len);
            temp[dp->name_len] = 0;
            printf("%4d\t%4d\t%4d\t\t%s\n", 
                            dp->inode, dp->rec_len, dp->name_len, temp);
            if(strcmp(temp, name) == 0) {
                return dp->inode;
            }

            memset(temp, 0, BLKSIZE);
            cp += dp->rec_len;
            dp = (DIR *) cp;
        }
    }

    return 0;
}


// retrun inode number of pathname

int getino(char *pathname)
{ 
    MINODE *mip;
    int ino = 0, num = 0;

    if(strcmp(pathname, "/") == 0) {
        return 2;
    }

    if (pathname[0] == '/') 
        mip = root;
    else
        mip = running->cwd;
    
    num = tokenize(pathname);

    for(int i = 0; i < num; i++) {
        if(!S_ISDIR(mip->INODE.i_mode)) {
            printf("%s is not a directory \n", name[i]);
            iput(mip);
            return 0;
        }
        ino = search(mip, name[i]);
        iput(mip);

        if (!ino) {
            printf("%s does not exist\n", name[i]);
            return 0;
        }
        
        mip = iget(dev, ino);
    }
    
    iput(mip);
    return ino;
}



// THESE two functions are for pwd(running->cwd), which prints the absolute
// pathname of CWD. 

int findmyname(MINODE *parent, u32 myino, char *myname) 
{
   // parent -> at a DIR minode, find myname by myino
   // get name string of myino: SAME as search except by myino;
   // copy entry name (string) into myname[ ];
}


int findino(MINODE *mip, u32 *myino) 
{
    char sbuf[BLKSIZE];
    get_block(mip->dev, mip->INODE.i_block[0], sbuf);

    DIR *temp;
    char name[BLKSIZE] = "\0";

    temp = (DIR *)sbuf;
    char *cp = sbuf;
    
    while(cp < sbuf + BLKSIZE) {
        if(strcmp(temp->name, ".") == 0) {
            myino = temp->inode;
        }
        
        if(strcmp(temp->name, "..") == 0) {
            return temp->inode;
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }
    return -1;
}