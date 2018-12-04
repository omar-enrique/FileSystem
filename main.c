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
    char sbuf[BLKSIZE], gbuf[BLKSIZE];
    MOUNT *mp;

    dev = open("disk", O_RDWR);
    if(dev < 0) {
        printf("Crash: Cannot open %s for read/write\n", disk);
        exit(1);
    }

    get_block(dev, 1, sbuf);
    sp = (SUPER *) sbuf;

    if(sp->s_magic != EXT2_SUPER_MAGIC) {
        printf("Crash: %s is not an EXT2 Filesystem", disk);
        exit(1);
    }
    mp = &mtable[0];
    mp->dev = dev;

    get_block(dev, 2, gbuf);
    gp = (GD *) gbuf;

    nblocks = sp->s_blocks_count;
    ninodes = sp->s_inodes_count;
    freeinodes = sp->s_free_inodes_count;
    freeblocks = sp->s_free_blocks_count;
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

    update_cwd(running->cwd);
}

void update_cwd(MINODE *dir)
{
    int ino;
    char filename[255];
    INODE ip;
    MINODE *mip;
    if (dir->ino == 2)
    {
        strcpy(cwd, "/");
        return;
    }

    if ((ino = search(dir, "..")) == 2) //If your parent directory is root
    {
        mip = iget(dir->dev, 2);
        //Load the name of the current directory
        findmyname(mip, dir->ino, filename);
        sprintf(cwd, "/%s", filename);

        int length = strlen(cwd);
        cwd[length -1] = 0;
        //cwd[strlen(cwd) - 1] = 0;
        return;
    }
    else
    {
        mip = iget(dir->dev, ino);
        rpwd(mip);
        //Load the name of the current directory
        findmyname(mip, dir->ino, filename);
        strcat(cwd, "/");
        strcat(cwd, filename);

        int length = strlen(cwd);
        cwd[length] = 0;
        return;
    }
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
    printf("/%s", dirname);
}

int my_mkdir(char *pathname) {
    char temp[1024];
    char buf[BLKSIZE];
    DIR *newDP = (DIR *) buf;
    char *parentname;
    char *base;
    int parentino = 0, ino = 0, blk = 0;
    char *cp = buf;

    MINODE *mip, *parentmip;
    INODE *newIP;

    strcpy(temp, pathname);

    parentname = dirname(pathname);   

    base = basename(temp);
    
    if((parentino = getino(parentname)) < 0) {
        printf("Pathname is invalid.\n");
        return 0;
    }
    parentmip = iget(dev, parentino);

    if(!S_ISDIR(parentmip->INODE.i_mode)) {
        printf("Pathname does not lead to a directory\n");
        return 0;
    }
    if(search(parentmip, base)) {
        printf("Directory already exists\n");
        return 0;
    }
    ino = ialloc(dev);
    blk = balloc(dev);
    printf("INO: %d, BLK: %d\n", ino, blk);
    mip = iget(dev, ino);
    get_block(dev, blk, buf);

    newIP = &mip->INODE;
    newIP->i_mode = 0x41ED;
    // 040755: DIR type and permissions
    newIP->i_uid = running->uid; // owner uid
    newIP->i_gid = running->gid; // group Id
    newIP->i_size = BLKSIZE;
    newIP->i_links_count = 2;
    // links count=2 because of . and ..
    newIP->i_atime = newIP->i_ctime = newIP->i_mtime = time(0L);
    newIP->i_blocks = 2;
    // LINUX: Blocks count in 512-byte chunks
    newIP->i_block[0] = blk;
    for(int i = 1; i < 15; i++) {
        newIP->i_block[i] = 0;
    }
    mip->dirty = 1;
    iput(mip);
    
    newDP->inode = ino;
    newDP->rec_len = 12;
    newDP->name_len  = 1;
    newDP->name[0] = '.';
    newDP->file_type = (u8)EXT2_FT_DIR;

    cp += newDP->rec_len;
    newDP = (DIR*)cp;

    newDP->inode = parentino;
    newDP->rec_len = BLKSIZE - 12;
    newDP->name_len = 2;
    newDP->file_type = (u8)EXT2_FT_DIR; //EXT2 dir type
    newDP->name[0] = newDP->name[1] = '.';
    put_block(dev, blk, buf);

    enter_name(parentmip, ino, base, EXT2_FT_DIR);

    return 1;
}

int my_creat(char *pathname){

    char temp[1024];
    char buf[BLKSIZE];
    DIR *newDP = (DIR *) buf;
    char *parentname;
    char *base;
    int parentino = 0, ino = 0, blk = 0;
    char *cp = buf;

    MINODE *mip, *parentmip;
    INODE *newIP;

    strcpy(temp, pathname);

    parentname = dirname(pathname);   

    base = basename(temp);
    
    if((parentino = getino(parentname)) < 0) {
        printf("Pathname is invalid.\n");
        return 0;
    }
    parentmip = iget(dev, parentino);

    if(!S_ISDIR(parentmip->INODE.i_mode)) {
        printf("Pathname does not lead to a directory\n");
        return 0;
    }

    if(search(parentmip, base)) {
        printf("File already exists\n");
        return 0;
    }
    ino = ialloc(dev);
    blk = balloc(dev);
    printf("INO: %d, BLK: %d\n", ino, blk);
    mip = iget(dev, ino);
    get_block(dev, blk, buf);

    newIP = &mip->INODE;
    newIP->i_mode = 0x81A4;
    newIP->i_uid = running->uid; // owner uid
    newIP->i_gid = running->gid; // group Id
    newIP->i_size = 0;
    newIP->i_links_count = 1;
    newIP->i_atime = newIP->i_ctime = newIP->i_mtime = time(0L);
    newIP->i_blocks = 0;
    

    for(int i = 0; i < 15; i++) {
        newIP->i_block[i] = 0;
    }

    mip->dirty = 1;
    iput(mip);
    
    enter_name(parentmip, ino, base, EXT2_FT_REG_FILE);

    return 1;
}

int enter_name(MINODE *mip, int myino, char *myname, int fileType)
{
    int i;
    INODE *parent_ip = &mip->INODE;
    
    char buf[1024];
    char *cp;
    DIR *newDP;

    int need_len = 0, ideal = 0, remain = 0;
    int bno = 0, block_size = 1024;

    //go through parent data blocks
    for (i = 0; i < parent_ip->i_size / BLKSIZE; i++)
    {
        if (parent_ip->i_block[i] == 0)
            break; //empty data block, break

        //get bno to use in get_block
        bno = parent_ip->i_block[i];

        get_block(dev, bno, buf);

        newDP = (DIR *)buf;
        cp = buf;

        //need length
        need_len = 4 * ((8 + strlen(myname) + 3) / 4);
        printf("need len is %d\n", need_len);

        //step into last dir entry
        while (cp + newDP->rec_len < buf + BLKSIZE)
        {
            cp += newDP->rec_len;
            newDP = (DIR *)cp;
        }

        printf("last entry is %s\n", newDP->name);
        cp = (char *)newDP;

        //ideal length uses name len of last dir entry
        ideal = 4 * ((8 + newDP->name_len + 3) / 4);

        //let remain = last entry's rec_len - its ideal length
        remain = newDP->rec_len - ideal;
        printf("remain is %d\n", remain);

        if (remain >= need_len)
        {
            //enter the new entry as the last entry and trim the previous entry to its ideal length
            newDP->rec_len = ideal;

            cp += newDP->rec_len;
            newDP = (DIR *)cp;

            newDP->inode = myino;
            newDP->rec_len = block_size - ((u32)cp - (u32)buf);
            printf("rec len is %d\n", newDP->rec_len);
            newDP->name_len = strlen(myname);
            newDP->file_type = fileType;
            strcpy(newDP->name, myname);

            put_block(dev, bno, buf);

            return 1;
        }
    }

    printf("Number is %d...\n", i);

    //no space in existing data blocks, time to allocate in next block
    bno = balloc(dev);           //allocate blocks
    parent_ip->i_block[i] = bno; //add to parent

    parent_ip->i_size += BLKSIZE; //modify inode size
    mip->dirty = 1;

    get_block(dev, bno, buf);

    newDP = (DIR *)buf; //dir pointer modified
    cp = buf;

    printf("Dir name is %s\n", newDP->name);

    newDP->inode = myino;             //set inode to myino
    newDP->rec_len = 1024;            //reset length to 1024
    newDP->name_len = strlen(myname); //set name to myname
    newDP->file_type = EXT2_FT_DIR;   //set dir type to EXT2 compatible
    strcpy(newDP->name, myname);      //set the dir pointer name to myname

    put_block(dev, bno, buf); //add the block

    return 1;
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
        otable->offset = mip->INODE.i_size;
    }

    for(int i = 0; i < NFD; i++) {
        if(!running->fd[i]) {
            running->fd[i] = otable;
            printf("OFFSET: %d\n\n", otable->offset);
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
    char kbuf[BLKSIZE];
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
            get_block(mip->dev, mip->INODE.i_block[lblk], kbuf);
        }
        else if(lblk < 12 + 256) {
            get_block(mip->dev, mip->INODE.i_block[12], indirect);
            get_block(mip->dev, indirect[lblk - 12], kbuf);
        } 
        
        cp = kbuf + start;
        remain = BLKSIZE - start;

        int smaller = nbytes;
        if(avil < nbytes )
            smaller = avil;

        countBytes += smaller;

        offset += smaller;

        if(smaller <= BLKSIZE)
        {
            strncpy(buf, cp, smaller);

            break;
        }
        else {
            strncpy(buf, cp, BLKSIZE);
            avil-= 1024;
            nbytes -= 1024;
        }

    }

    running->fd[fd]->offset = offset;
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

int my_cat(char *pathname) {
    int index = 0;
    char buf[BLKSIZE];
    index = my_open(pathname, 0);
    if(index < 0){
        printf("File not opened\n");
    }
    while(myread(index, buf, BLKSIZE))
        printf("%s", buf);
    return 0;
}

void link(char *pathname)
{
    char source[64], destination[64], temp[64];
    char link_parent[64], link_child[64];
    int ino;
    int p_ino;
    MINODE *mip;   //miniode pointer
    MINODE *p_mip; //another minode pointer
    INODE *ip;     //inode pointer
    INODE *p_ip;   //another inode pointer

    //Checks
    if (!strcmp(pathname, ""))
    {
        printf("No source file!\n");
        return;
    }

    if (!strcmp(three, ""))
    {
        printf("No new file!\n");
        return;
    }

    strcpy(source, pathname); //make some copies
    strcpy(destination, three);

    //get sourcefilename's inode
    ino = getino(source);
    mip = iget(dev, ino);

    //verify source file exists
    if (!mip)
    {
        printf("%s does not exist!\n", source);
        return;
    }

    //Verify it is a file
    if (S_ISDIR(mip->INODE.i_mode))
    {
        printf("Can't link a directory!\n");
        return;
    }

    //get destination's dirname
    if (!strcmp(destination, "/"))
    {
        strcpy(link_parent, "/");
    }
    else
    {
        strcpy(temp, destination);
        strcpy(link_parent, dirname(temp));
    }

    //get destination's basename
    strcpy(temp, destination);
    strcpy(link_child, basename(temp));

    //get new's parent
    p_ino = getino(link_parent);
    p_mip = iget(dev, p_ino);

    //verify that link parent exists
    if (!p_mip)
    {
        printf("No parent!\n");
        return;
    }

    //verify link parent is a directory
    if (!S_ISDIR(p_mip->INODE.i_mode))
    {
        printf("Not a directory\n");
        return;
    }

    //verify that link child does not exist yet
    if (getino(destination) != -1)
    {
        printf("%s already exists\n", destination);
        return;
    }

    //enter the name for the newfile into the parent dir
    printf("Entering name for %s\n", link_child);

    //this ino is the ino of the source file
    enter_name(p_mip, ino, link_child, EXT2_FT_REG_FILE);

    ip = &mip->INODE;

    //increment the link count cuz this is a link.. cuz!
    ip->i_links_count++;
    
    mip->dirty = 1;
    p_ip = &p_mip->INODE;
    p_ip->i_atime = time(0L);
    p_mip->dirty = 1;

    iput(p_mip);
    iput(mip);
    return;
}

int unlink(char *pathname)
{
	char path[strlen(pathname) + 1];
	strcpy(path, pathname);
	char *base = basename(pathname);
	char *dir = NULL;

	if(strcmp(base, path) == 0)
	{
		dir = dirname(path);
	}

	int dev;
	int parentino = getino(dir);
	if(parentino == -1)
	{
		printf("Could not find parent.\n");
		return -1;
	}

	MINODE *pip = iget(running->cwd->dev, parentino);
	if((pip->INODE.i_mode & 0x4000) != 0x4000)
	{
		iput(pip);
		printf("Parent is not a directory.\n");
		return -1;
	}

	// remove direntry
	int rem = removeDirEntry(pip, base);
	iput(pip);
    if (rem == -1)
    {
        printf("Unabe to remove link.\n");
        return -1;
    }
    return 0;
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

int removeDirEntry(MINODE *parent, const char* name)
{
    char buf[BLKSIZE];
    for(int i = 0; i < 12; ++i)
    {
        if(parent->INODE.i_block[i] != 0)
        {
            get_block(parent->dev, parent->INODE.i_block[i], buf);
            DIR *dir = (DIR *)&buf;
            DIR *prevdir;
            int pos = 0;
            char *loc = (char *)dir;
            
            while(pos < BLKSIZE)
            {
                char dirname[dir->name_len + 1];
                strncpy(dirname, dir->name, dir->name_len);
                dirname[dir->name_len] = '\0';

				if(!strcmp(dirname, name))  // found dir
                {
                    if (pos + dir->rec_len == BLKSIZE)   //last entry
                    {
						if (pos == 0)   // only entry in block
                        {
                            int tmp = parent->INODE.i_block[i];
                            parent->INODE.i_block[i] = 0;
                            bdealloc(parent->dev, tmp);
							parent->dirty = 1;
                            return 0;
                        }

						prevdir->rec_len += dir->rec_len;
						put_block(parent->dev, parent->INODE.i_block[i], buf);
                        return 0;
                    }
                    
                    prevdir = dir;  // set tail
                    int posb = pos;
                    
                    // move head rec_len bytes
                    loc = (char *)dir;
                    loc += dir->rec_len;
                    posb += dir->rec_len;
                    dir = (DIR *)loc;
                   
                    int remlen = prevdir->rec_len; // leftover space
                    
                    while(posb < BLKSIZE)
                    {
                        prevdir->rec_len = dir->rec_len;    // assign
                        
                        // copy head to tail
                        prevdir->inode = dir->inode;
                        prevdir->file_type = dir->file_type;
                        prevdir->name_len = dir->name_len;
                        memcpy(prevdir->name, dir->name, dir->name_len);
                        
                        // give rest of space to prevdir
                        if (posb + dir->rec_len == BLKSIZE)
                        {
                            prevdir->rec_len += remlen;
							put_block(parent->dev, parent->INODE.i_block[i], buf);
                            return 0;
                        }
                        
                        // move head rec_len bytes
                        loc = (char *)dir;
                        loc += dir->rec_len;
                        posb += dir->rec_len;
                        dir = (DIR *)loc;
                        
                        // move tail rec_len bytes
                        loc = (char *)prevdir;
                        loc += prevdir->rec_len;
                        posb += prevdir->rec_len;
                        prevdir = (DIR *)loc;
                    }
                }
                prevdir = dir;
                
                // move rec_len bytes
                loc = (char *)dir;
                loc += dir->rec_len;
				pos += dir->rec_len;
                dir = (DIR *)loc;
                
            }
        }
    }
	return -1;
}

int symlink(char *pathname)
{
	char *src = pathname;
	char *destination = three;

	char tmpa[strlen(src) + 1];
	strcpy(tmpa, src);

	//int dev;
	int ino = getino(src);
	if(ino == -1)
	{
		printf("Invalid source path.\n");
		return -1;
	}
    MINODE *mipsrc = iget(dev, ino);

	if(!S_ISDIR(mipsrc->INODE.i_mode) && (!S_ISREG(mipsrc->INODE.i_mode)))
	{
		iput(mipsrc);
		printf("Source is not a regular file or a directory.\n");
		return -1;
	}
	
	char path[strlen(destination) + 1];
	strcpy(path, destination);
	char *base = basename(destination);
	char *dir = NULL;
	if(strcmp(base, path) == 0)
	{
		dir = dirname(path);
	}

	ino = getino(dir);
	if(ino == -1)
	{
		iput(mipsrc);
		printf("Invalid target path\n");
		return -1;
	}
    MINODE *mipparent = iget(dev, ino);
	if(!S_ISDIR(mipparent->INODE.i_mode))
	{
		iput(mipsrc);
		iput(mipparent);
		printf("Target path is not a directory.\n");
		return -1;
	}

    //get a new inode and send it to the naming function as a symlink
	ino = ialloc(dev);
	enter_name(mipparent, ino, base, EXT2_FT_SYMLINK);

    MINODE *mip = iget(dev, ino);

    //set those good ol properties
	mip->INODE.i_mode = 0120000;
	mip->INODE.i_uid = running->uid;
	mip->INODE.i_gid = running->gid;
	mip->INODE.i_size = 0;
	mip->INODE.i_links_count = 0;
	mip->INODE.i_atime = mip->INODE.i_ctime = mip->INODE.i_mtime = time(0);
	mip->INODE.i_blocks = 0;
	mip->dirty = 1;

    //zero the blocks
	for(int i = 0; i < 15; ++i)
	{
		mip->INODE.i_block[i] = 0;
    }

    //Increment the link ocunt
	mip->INODE.i_links_count++;
	strcpy((char *)mip->INODE.i_block, tmpa);

    //stash em!
    //side note.. this project is wearing thin on my patience
	iput(mip);
	iput(mipsrc);
	iput(mipparent);
	
    return 0;
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
        sscanf(line, "%s %s %s", cmd, pathname, three);

        if(strcmp(cmd, "cd") == 0) {
            ch_dir(pathname);
        }
        else if(strcmp(cmd, "ls") == 0) {
            ls(pathname);
        }
        else if(strcmp(cmd, "pwd") == 0) {
            pwd(cwd);
        }
        else if(strcmp(cmd, "cat") == 0) {
            my_cat(pathname);
        }
        else if(strcmp(cmd, "mkdir") == 0) {
            my_mkdir(pathname);
        }
        else if(strcmp(cmd, "creat") == 0){
            my_creat(pathname);
        }
        else if(strcmp(cmd, "ln") == 0){
            link(pathname);
        }
        else if(strcmp(cmd, "unlink") == 0){
            unlink(pathname);
        }
        else if(strcmp(cmd, "symlink") == 0){
            symlink(pathname);
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