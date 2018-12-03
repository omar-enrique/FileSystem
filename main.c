#include "util.c"

char *disk = "disk";

int init() {
    for (int i=0; i<NMTABLE; i++)
        mtable[i].dev = 0;
    for(int i = 0; i < NMINODE; i++)
        minode[i].refCount = 0;
    for(int i = 0; i < NPROC; i++) {
        proc[i].pid = i;
        proc[i].uid = i;
        proc[i].cwd = 0;
        for(int j = 0; j < NFD; j++) {
            proc[i].fd[j] = 0;
        }
        proc[i].next = &proc[i+1];
    }
    proc[NPROC - 1].next = &proc[0];
    root = 0;
}

int mount_root() {
    char buf[BLKSIZE];
    MOUNT *mp;

    dev = open("disk", O_RDWR);
    if(dev < 0) {
        printf("Crash: Cannot open %s for read/write\n", disk);
        exit(1);
    }

    get_block(dev, 1, buf);
    sp = (SUPER *) buf;

    if(sp->s_magic != EXT2_SUPER_MAGIC) {
        printf("Crash: %s is not an EXT2 Filesystem", disk);
        exit(1);
    }
    mp = &mtable[0];
    mp->dev = dev;

    get_block(dev, 2, buf);
    gp = (GD *) buf;

    nblocks = sp->s_blocks_count;
    ninodes = sp->s_inodes_count;
    bmap = gp->bg_block_bitmap;
    imap = gp->bg_inode_bitmap;
    inode_start = gp->bg_inode_table;

    root = iget(dev, 2);
    strcpy(cwd, "/");
    root->mountptr = mp;
    mp->mounted_inode = root;

    proc[0].cwd = iget(dev, 2);
    proc[1].cwd = iget(dev, 2);

    running = &proc[0];
}

int ch_dir(char *pathname) {
    int ino = 0;
    MINODE *mip;
    if(strlen(pathname) < 1) {
        ino = 2;
    }
    else {
        ino = getino(pathname);
        if(ino == 0) {
            printf("File or directory not found.\n");
            return;
        } 
    }

    mip = iget(dev, ino);
    
    if(!S_ISDIR(mip->INODE.i_mode)) {
        printf("Not a directory\n");
        return;
    }


    iput(running->cwd);
    running->cwd = mip;
}

int ls(char *pathname) {
    MINODE *mip;
    int ino = 0;
    if(strlen(pathname) < 1) {
        printf("Directory / entries: \n", pathname);
        ino = running->cwd->ino;
    }
    else {
        ino = getino(pathname);
        
        if(ino == 0) {
            printf("File or directory not found.\n");
            return;
        }
    }
    mip = iget(dev, ino);

    if(S_ISDIR(mip->INODE.i_mode)) {
        ls_dir(mip);
        return;
    }
    else {
        printf("\nFile %s\n", pathname);
        return;
    }
}

int ls_dir(MINODE *mip) {
    char sbuf[BLKSIZE];
    DIR *temp;
    char name[BLKSIZE] = "\0";

    get_block(mip->dev, mip->INODE.i_block[0], sbuf);
    temp = (DIR *)sbuf;
    char *cp = sbuf;
    
    printf("Inode\tRec_len\tName_len\tName\n");
    while(cp < sbuf + BLKSIZE) {
        if((strcmp(temp->name, ".") != 0) && (strcmp(temp->name, "..") != 0)) {
            strncpy(name, temp->name, temp->name_len);
            printf("%4d\t%4d\t%4d\t\t%s\n", 
                temp->inode, temp->rec_len, temp->name_len, name);

            memset(name, 0, BLKSIZE-1);
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }

}

int ls_file(MINODE *mip) {
    char sbuf[BLKSIZE]; 
    DIR *temp;
    char dirname[BLKSIZE];
    int my_ino = 0;
    int parent_ino = 0;
    MINODE *pip;
    if (mip->ino == root->ino) {
        return;
    }
    get_block(mip->dev, mip->INODE.i_block[0], sbuf);
    int size = 0;
    temp = (DIR *)sbuf;
    char *cp = sbuf;

    while(cp < sbuf + BLKSIZE) {
        if(strcmp(temp->name, ".") == 0) {
            my_ino = temp->inode;
        }
        if(strcmp(temp->name, "..") == 0) {
            parent_ino = temp->inode;
            break;
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }


    pip = iget(dev, parent_ino); 
    get_block(dev, pip->INODE.i_block[0], sbuf);

    temp = (DIR *) sbuf;
    cp = sbuf;
    while(cp < sbuf + BLKSIZE) {
        strcpy(dirname, temp->name);
        dirname[temp->name_len] = 0;
        if(my_ino == temp->inode) {
            break;
        }
        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }
    printf("%s\n", dirname);
}

int rpwd(MINODE *wd) {
    char sbuf[BLKSIZE]; 
    DIR *temp;
    char dirname[BLKSIZE];
    int my_ino = 0;
    int parent_ino = 0;
    MINODE *pip;
    if (wd->ino == root->ino) {
        return;
    }
    get_block(wd->dev, wd->INODE.i_block[0], sbuf);
    int size = 0;
    temp = (DIR *)sbuf;
    char *cp = sbuf;

    while(cp < sbuf + BLKSIZE) {
        if(strcmp(temp->name, ".") == 0) {
            my_ino = temp->inode;
        }
        if(strcmp(temp->name, "..") == 0) {
            parent_ino = temp->inode;
            break;
        }

        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }

    pip = iget(dev, parent_ino); 
    get_block(dev, pip->INODE.i_block[0], sbuf);

    temp = (DIR *) sbuf;
    cp = sbuf;
    while(cp < sbuf + BLKSIZE) {
        strcpy(dirname, temp->name);
        dirname[temp->name_len] = 0;
        if(my_ino == temp->inode) {
            break;
        }
        cp = (char *) cp + temp->rec_len;       // advance cp by rec_len in BYTEs
        temp = (DIR *) cp;
    }

    rpwd(pip);
}

int make_dir(char *pathname) {
    char temp[1024];
    char *parentname;
    char *base;
    int ino = 0;

    MINODE *mip;

    strcpy(temp, pathname);

    parentname = dirname(pathname);
    base = basename(temp);

    if(!(ino = getino(pathname))) {
        printf("Pathname is invalid.\n");
        return 0;
    }

    mip = iget(dev, ino);

    if(!S_ISDIR(mip->INODE.i_mode)) {
        
    }

}

int dir_alloc() {

}

int pwd(MINODE *wd) {
    if (wd->ino == root->ino) {
        printf("/\n");
    }
    else {
        rpwd(wd);
        printf("\n");
    }
}

int my_open(char *file, int flags) {
    MINODE *mip;
    int ino = getino(file);
    if(ino == 0) {
        creat(file, 0077);
        ino = getino(file);
    }
    mip = iget(dev, ino);

    OFT *otable = (OFT *)malloc(sizeof(OFT *));

    otable->mode = flags;
    otable->minodePtr = mip;
    otable->refCount = 1;
    if(flags < 3)
        otable->offset = 0;
    else {
        struct stat st;
        stat(file, &st);
        otable->offset = st.st_size;
    }

    for(int i = 0; i < NFD; i++) {
        if(!running->fd[i]) {
            running->fd[i] = &otable;
            return i;
        }
    }

    return -1;
}

int my_lseek(int fd, int position) {
    OFT *temp = running->fd[fd];
    if(position <0 || position > temp->minodePtr->INODE.i_size){
		printf("Invalid position entered \n");
		return 0;
	}

    temp->offset = position;
    return 0;
} 

int myclose(int fd) {
    OFT *temp = running->fd[fd];
    if(running->fd[fd]) {
        temp->refCount--;
        if(temp->refCount == 0) {
            iput(temp->minodePtr);
        }
    }

    running->fd[fd] = 0;
}

int myread(int fd, char *buf, int nbytes) {
    int countBytes = 0;
    MINODE *mip;
    int lblk = 0, start = 0;
    char *kbuf;
    int remain = 0;
    char *cp;
    int *indirect;

    if(!running->fd[fd]) {
        printf("File not opened\n");
        return 0;
    }
    mip = running->fd[fd]->minodePtr;
    int offset = running->fd[fd]->offset;
    int avil = mip->INODE.i_size - offset;

    while(nbytes && avil) {
        lblk = offset / BLKSIZE;
        start = offset % BLKSIZE;

        if(lblk < 12) {
            get_block(dev, mip->INODE.i_block[lblk], kbuf);
        }
        else if(lblk < 12 + 256) {
            get_block(dev, mip->INODE.i_block[12], indirect);
            get_block(dev, indirect[lblk - 12], kbuf);
        } 
        
        cp = kbuf + start;
        remain = BLKSIZE - start;

        while(remain) {
            *buf++ = *cp++;
            offset++; countBytes++;
            remain--; avil--; nbytes--;
            if(nbytes <= 0 || avil <= 0) 
                break;
        }
    }

    return countBytes;
}

int my_write(int fd, char *buf, int nbytes) {
    int countBytes = 0;
    int lblk = 0, start = 0;
    int *indirect;
    char *cp;
    int remain;
    char *kbuf;
    int fileSize = 0;

    if(!running->fd[fd]) {
        printf("File not opened\n");
        return 0;
    }
    MINODE *mip = running->fd[fd]->minodePtr;
    int offset = running->fd[fd]->offset;
    fileSize = mip->INODE.i_size;
    while (nbytes) {
        lblk = offset / BLKSIZE;
        start = offset % BLKSIZE;

        if(lblk < 12) {
            get_block(dev, mip->INODE.i_block[lblk], kbuf);
        }
        else if(lblk < 12 + 256) {
            get_block(dev, mip->INODE.i_block[12], indirect);
            get_block(dev, indirect[lblk - 12], kbuf);
        } 

        cp = kbuf + start;
        remain = BLKSIZE - start;

        while(remain) {
            *cp++ = *buf++;
            offset++; countBytes++;
            remain--; nbytes--;
            if (offset > fileSize) fileSize++; 
            if (nbytes <= 0) break;
        }
    }

    mip->dirty  = 1;
    return countBytes;
}

int quit() {
    for(int i = 0; i < NMINODE; i++) {
        MINODE *mip = &minode[i];
        if(mip->refCount && mip->dirty) {
            mip->refCount = 1;
            iput(mip);
        }
    }
    exit(1);
}

int main(int argc, char *argv[]) {
    if(argc > 1) 
        disk = argv[1];
    init();
    mount_root();

    char buf[BLKSIZE];

    while(1) {
        printf("Pid running: %d \n", running->pid);

        printf("%s $ ", cwd);
        fgets(line, 128, stdin);
        line[strlen(line) - 1] = 0;
        if(line[0] == 0)
            continue;
        sscanf(line, "%s %s", cmd, pathname);

        if(strcmp(cmd, "cd") == 0) {
            ch_dir(pathname);
        }
        else if(strcmp(cmd, "ls") == 0) {
            ls(pathname);
        }
        else if(strcmp(cmd, "pwd") == 0) {
            pwd(running->cwd);
        }
        else if(strcmp(cmd, "quit") == 0) {
            quit();
        }
        else {
            printf("Invalid Command\n");
        }
        printf("\n");

        memset(pathname, 0, 256);
    }
}