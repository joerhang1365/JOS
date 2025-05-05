#include "conf.h"
#include "assert.h"
#include "intr.h"
#include "device.h"
#include "thread.h"
#include "heap.h"
#include "dev/virtio.h"
#include "dev/uart.h"
#include "timer.h"
#include "ktfs.h"
#include "io.h"
#include "fs.h"
#include "string.h"
#include "memory.h"

#define LOCK_ITER 5

void lock_test_fn(struct lock * test_lock, int iter);
void test_ktfs();
void test_open_files();
void test_open_files2();


extern char _kimg_blob_start[];
extern char _kimg_blob_end[];
void main(void) {
    // extern char _kimg_end[];
    int i;

    intrmgr_init();
    timer_init();
    devmgr_init();
    thrmgr_init();

    //heap_init(_kimg_end, RAM_END);
    memory_init();

    for (i = 0; i < 3; i++)
        uart_attach((void*)UART_MMIO_BASE(i), UART_INTR_SRCNO(i));

    for (i = 0; i < 8; i++)
        virtio_attach((void*)VIRTIO_MMIO_BASE(i), VIRTIO_INTR_SRCNO(i));

    //test_ktfs();
    test_open_files2();


}
void test_open_files2(){
    //unsigned long long len;
    //unsigned long long new_len;
    unsigned long long files = 96;
    int result;
    kprintf("hello \n");

    struct io * blkio;

    result = open_device("vioblk", 0, &blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }

    struct io *file_list[files];

    fsmount(blkio);

    fscreate("file0");
    fscreate("file1");
    fscreate("file2");
    fscreate("file3");
    fscreate("file4");
    fscreate("file5");
    fscreate("file6");
    fscreate("file7");
    fscreate("file8");
    fscreate("file9");
    fscreate("file10");
    fscreate("file11");
    fscreate("file12");
    fscreate("file13");
    fscreate("file14");
    fscreate("file15");
    fscreate("file16");
    fscreate("file17");
    fscreate("file18");
    fscreate("file19");
    fscreate("file20");
    fscreate("file21");
    fscreate("file22");
    fscreate("file23");
    fscreate("file24");
    fscreate("file25");
    fscreate("file26");
    fscreate("file27");
    fscreate("file28");
    fscreate("file29");
    fscreate("file30");
    fscreate("file31");
    fscreate("file32");
    fscreate("file33");
    fscreate("file34");
    fscreate("file35");
    fscreate("file36");
    fscreate("file37");
    fscreate("file38");
    fscreate("file39");
    fscreate("file40");
    fscreate("file41");
    fscreate("file42");
    fscreate("file43");
    fscreate("file44");
    fscreate("file45");
    fscreate("file46");
    fscreate("file47");
    fscreate("file48");
    fscreate("file49");
    fscreate("file50");
    kprintf("created file: %d\n", fscreate("file51"));
    kprintf("created file: %d\n", fscreate("file52"));
    kprintf("created file: %d\n", fscreate("file53"));
    kprintf("created file: %d\n", fscreate("file54"));
    kprintf("created file: %d\n", fscreate("file55"));
    kprintf("created file: %d\n", fscreate("file56"));
    kprintf("created file: %d\n", fscreate("file57"));
    kprintf("created file: %d\n", fscreate("file58"));
    kprintf("created file: %d\n", fscreate("file59"));
    kprintf("created file: %d\n", fscreate("file60"));
    kprintf("created file: %d\n", fscreate("file61"));
    kprintf("created file: %d\n", fscreate("file62"));
    kprintf("created file: %d\n", fscreate("file63"));
    kprintf("created file: %d\n", fscreate("file64"));
    kprintf("created file: %d\n", fscreate("file65"));
    kprintf("created file: %d\n", fscreate("file66"));
    kprintf("created file: %d\n", fscreate("file67"));
    kprintf("created file: %d\n", fscreate("file68"));
    kprintf("created file: %d\n", fscreate("file69"));
    kprintf("created file: %d\n", fscreate("file70"));
    kprintf("created file: %d\n", fscreate("file71"));
    kprintf("created file: %d\n", fscreate("file72"));
    kprintf("created file: %d\n", fscreate("file73"));
    kprintf("created file: %d\n", fscreate("file74"));
    kprintf("created file: %d\n", fscreate("file75"));
    kprintf("created file: %d\n", fscreate("file76"));
    kprintf("created file: %d\n", fscreate("file77"));
    kprintf("created file: %d\n", fscreate("file78"));
    kprintf("created file: %d\n", fscreate("file79"));
    kprintf("created file: %d\n", fscreate("file80"));
    kprintf("created file: %d\n", fscreate("file81"));
    kprintf("created file: %d\n", fscreate("file82"));
    kprintf("created file: %d\n", fscreate("file83"));
    kprintf("created file: %d\n", fscreate("file84"));
    kprintf("created file: %d\n", fscreate("file85"));
    kprintf("created file: %d\n", fscreate("file86"));
    kprintf("created file: %d\n", fscreate("file87"));
    kprintf("created file: %d\n", fscreate("file88"));
    kprintf("created file: %d\n", fscreate("file89"));
    kprintf("created file: %d\n", fscreate("file90"));
    kprintf("created file: %d\n", fscreate("file91"));
    kprintf("created file: %d\n", fscreate("file92"));
    kprintf("created file: %d\n", fscreate("file93"));
    kprintf("created file: %d\n", fscreate("file94"));
    kprintf("created file: %d\n", fscreate("file95"));
    kprintf("created file: %d\n", fscreate("file96"));

    //kprintf("created file: %d\n", fscreate("file89"));












    kprintf("Opened file: %d\n",fsopen("file0", &file_list[0]));
    kprintf("Opened file: %d\n",fsopen("file1", &file_list[1]));
    kprintf("Opened file: %d\n",fsopen("file2", &file_list[2]));
    kprintf("Opened file: %d\n",fsopen("file3", &file_list[3]));
    kprintf("Opened file: %d\n",fsopen("file4", &file_list[4]));
    kprintf("Opened file: %d\n",fsopen("file5", &file_list[5]));
    kprintf("Opened file: %d\n",fsopen("file6", &file_list[6]));
    kprintf("Opened file: %d\n",fsopen("file7", &file_list[7]));
    kprintf("Opened file: %d\n",fsopen("file8", &file_list[8]));
    kprintf("Opened file: %d\n",fsopen("file9", &file_list[9]));
    kprintf("Opened file: %d\n",fsopen("file10", &file_list[10]));
    kprintf("Opened file: %d\n",fsopen("file11", &file_list[11]));
    kprintf("Opened file: %d\n",fsopen("file0", &file_list[12]));
    kprintf("Opened file: %d\n",fsopen("file1", &file_list[13]));
    kprintf("Opened file: %d\n",fsopen("file2", &file_list[14]));
    kprintf("Opened file: %d\n",fsopen("file3", &file_list[15]));
    kprintf("Opened file: %d\n",fsopen("file4", &file_list[16]));
    kprintf("Opened file: %d\n",fsopen("file5", &file_list[17]));
    kprintf("Opened file: %d\n",fsopen("file6", &file_list[18]));
    kprintf("Opened file: %d\n",fsopen("file7", &file_list[19]));
    kprintf("Opened file: %d\n",fsopen("file8", &file_list[20]));
    kprintf("Opened file: %d\n",fsopen("file9", &file_list[21]));
    kprintf("Opened file: %d\n",fsopen("file10", &file_list[22]));
    kprintf("Opened file: %d\n",fsopen("file11", &file_list[23]));


    kprintf("Opened file86: %d\n",fsopen("file86", &file_list[19]));
    kprintf("Opened file87: %d\n",fsopen("file87", &file_list[20]));
    kprintf("Opened file88: %d\n",fsopen("file88", &file_list[20]));










}


void test_ktfs(){
    unsigned long long len;
    unsigned long long new_len;
    int result;
    kprintf("hello \n");
    uint64_t blob_size = ((uint64_t) &_kimg_blob_end - (uint64_t) &_kimg_blob_start);

    kprintf("Blob_size: %u \n", blob_size);
    // struct io * memio = create_memory_io(&_kimg_blob_start, blob_size);
    struct io * blkio;

    result = open_device("vioblk", 0, &blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }

    struct io * my_file = NULL;
    struct io * my_file2 = NULL;
    struct io * my_file3 = NULL;
    struct io * my_file4 = NULL;
    struct io * my_file5 = NULL;
    struct io * my_file6 = NULL;
    struct io * my_file7 = NULL;
    // struct io * my_file8 = NULL;

    uint32_t block_count;

    ioreadat(blkio, 8UL, &block_count, sizeof(uint32_t));

    kprintf("Inode Block count: %d \n", block_count);


    fsmount(blkio);
    kprintf("Open: %d \n",fsopen("jeff.txt", &my_file));



    // char c[12];
    // uint32_t pos = 0x1320;
    // ioreadat(my_file, pos, c, 12*sizeof(char));

    // for(int i = 0; i < 12; i++){
    //     kprintf("Char: %c \n", c[i]);
    // }
    // kprintf("\n");

    // uint32_t ktfs_get_new_block();
    // int ktfs_release_block(uint32_t block_id);
    // int ktfs_get_new_inode();
    // int ktfs_release_inode(uint16_t inode_id);

    // kprintf("New Block id:%d \n", ktfs_get_new_block());
    // kprintf("New Block id:%d \n", ktfs_get_new_block());
    // ktfs_release_block(87);
    // kprintf("New Block id:%d \n", ktfs_get_new_block());


    // for(int i = 0 ; i < 10; i++){
    //     kprintf("New inode id: %d \n", ktfs_get_new_inode());
    // }
    // ktfs_release_inode(7);
    // ktfs_release_inode(10);
    // kprintf("New inode id: %d \n", ktfs_get_new_inode());
    // kprintf("New inode id: %d \n", ktfs_get_new_inode());
    // kprintf("New inode id: %d \n", ktfs_get_new_inode());

    // char * string_write = "hello my name is jeff\0";
    // char * string_write2 = "biglev is watching\0";
    // unsigned long long len = 8;

    // char string_read[16];

    // iowriteat(my_file, 0x1320, string_write, len);

    // ioreadat(my_file, 0x1320, string_read, len);

    // kprintf("%s\n", string_read);


    // iowriteat(my_file, 0x10, string_write2, len);
    // ioreadat(my_file, 0x10, string_read, len);
    // kprintf("%s\n", string_read);





    //ktfs->intf->readat(ktfs, )



    // char * string_write = "Some of you are playing videogames during class. Not very well I might add\0";

    // unsigned long long new_len = 512;
    // //ioctl(my_file, 3, &new_len);


    // len = 76;
    // char string_read[len];

    // iowriteat(my_file, 500, string_write, len);

    // ioreadat(my_file, 500, string_read, len);

    // for(int i = 0; i < len; i++){
    //     kprintf("%c", string_read[i]);
    // }
    // kprintf("\n");



    fscreate("lev");
    len = 38;
    char string_read[len];
    kprintf("Opened file2: %d \n",fsopen("lev", &my_file2));
    char * string_write2 = "ASU has a great online degree program\0";
    new_len = 100000;
    ioctl(my_file2, 3, &new_len);

    iowriteat(my_file2, 500, string_write2, len);

    ioreadat(my_file2, 500, string_read, len);

    for(int i = 0; i < len; i++){
        kprintf("%c", string_read[i]);
    }
    kprintf("\n");


    //TODO Write case only 100 blocks and see if you can find them in file



    //kprintf("FS delete Val: %d \n", fsdelete("jeff.txt"));


    // kprintf("New inode id: %d \n", ktfs_get_new_inode());


    kprintf("fsopen val: %d \n", fsopen("jeff.txt", &my_file));

    //char * string_write3 = "I drive a shitty car still";


    char * bigstringinheap = alloc_phys_pages(3);
    char * biglev = "Onceuponatimeinaquietvalleysurroundedbytallhillsandwhisperingtrees,therewasasmallvillagewherethedaysmovedslowlyandthenightswerefilledwithstars.Thepeopleofthevillagelivedsimplelives.Theyworkedduringtheday,tendedtheirgardens,caredfortheiranimals,andsharedstoriesbycandlelightwhenthesunwentdown.InthisvillagelivedanoldmannamedElias.Hehadalongwhitebeardandkindeyesthatsparkledwithstoriesuntold.Eliasspentmostofhisdayssittingonawoodenbenchoutsidehiscottage,watchingtheworldgoby.Childrenwouldoftengatheraroundhim,eagertohearoneofhismanytalesaboutdistantlands,mysteriouscreatures,andbraveheroes.OneparticularstoryhetoldoftenwasaboutafoxnamedAlric,cleverandswift,wholiveddeepintheforestbeyondthehills.Alricwasnoordinaryfox.Hehadaheartofgoldandasharpmind,andhehelpedthoseinneedwhentheworldturnedcold.Heoncesavedabirdwithabrokenwing,guidedalosttravelerhome,andoutwittedagreedywolfwhotriedtostealfoodfromtheforestfolk.Thechildrennevertiredofthestory,andEliasnevertiredoftellingit.Thevillagerswouldsmileastheypa";
    memcpy(bigstringinheap, biglev, 1000);
    biglev = "ssedby,hearinghisvoiceriseandfallwiththerhythmofthetale.Itbecameapartofvillagelife,liketheringingofthemorningbellorthesmelloffreshbreadfromthebakersoven.ButEliasknewthatstoriesweremorethanjustentertainment.Theywerethreadsthatconnectedpeopleacrossgenerations.Theywerehowmemorieslivedonandhowvalueswerepasseddown.Everytimehetoldatale,hefelthewasaddingsomethinggoodtotheworld—alittlewarmth,alittlewonder.Astheyearswenton,Eliasgrewslower.Hisstepsweresmaller,hisvoicequieter,buthestillsatonthatsamebench,stilltoldthosesamestories.Thechildrenwhooncesatcross-leggedathisfeetgrewintoadultswithstoriesoftheirown,buttheyneverforgotEliasorthefoxnamedAlric.Eventually,Eliaspassedon.Thevillagemourned,buthislegacyremained.Inthetownsquare,theyplacedacarvedwoodenbenchinhishonor,andonit,theyinscribedthewords:ToElias,thestoryteller,whosetaleslittheheartsofmany.Thechildrennowgrowntoldhisstoriestotheirchildren,whotoldthemtotheirs.Yearsturnedtodecades,butthestorieslivedon.Andifyoueverfindyourselfinthatquietvalle";
    memcpy(bigstringinheap + 1000, biglev, 1000);
    biglev = "y,andyousitonthatbenchbeneaththeoldoaktree,youjustmighthearthewindwhisperingthetaleofAlricthefox—clever,kind,andalwaysreadytohelp.Inaworldthatoftenfeelstoofast,tooloud,toofilledwithnoise,storieslikethoseofEliasandAlricremindustoslowdownandlisten.Theyremindusthatkindnessmatters,thathelpingothersisworthit,andthatimaginationisapowerfulthing.Sotakeamoment,whereveryouare,andremember:thereismagicinwords,andthereisbeautyintellingthem.Andmaybe,justmaybe,onedaysomeonewillsitbesideyou,askforastory,andyoullsmile,takeadeepbreath,andbeginwiththosetimelesswords:Onceuponatime…Onceuponatimeinaquietvalleysurroundedbytallhillsandwhisperingtrees,therewasasmallvillagewherethedaysmovedslowlyandthenightswerefilledwithstars.Thepeopleofthevillagelivedsimplelives.Theyworkedduringtheday,tendedtheirgardens,caredfortheiranimals,andsharedstoriesbycandlelightwhenthesunwentdown.InthisvillagelivedanoldmannamedElias.Hehadalongwhitebeardandkindeyesthatsparkledwithstoriesuntold.Eliasspentmostofhisdayssittingonawood";
    memcpy(bigstringinheap + 2000, biglev, 1000);
    biglev = "enbenchoutsidehiscottage,watchingtheworldgoby.Childrenwouldoftengatheraroundhim,eagertohearoneofhismanytalesaboutdistantlands,mysteriouscreatures,andbraveheroes.OneparticularstoryhetoldoftenwasaboutafoxnamedAlric,cleverandswift,wholiveddeepintheforestbeyondthehills.Alricwasnoordinaryfox.Hehadaheartofgoldandasharpmind,andhehelpedthoseinneedwhentheworldturnedcold.Heoncesavedabirdwithabrokenwing,guidedalosttravelerhome,andoutwittedagreedywolfwhotriedtostealfoodfromtheforestfolk.Thechildrennevertiredofthestory,andEliasnevertiredoftellingit.Thevillagerswouldsmileastheypassedby,hearinghisvoiceriseandfallwiththerhythmofthetale.Itbecameapartofvillagelife,liketheringingofthemorningbellorthesmelloffreshbreadfromthebakersoven.ButEliasknewthatstoriesweremorethanjustentertainment.Theywerethreadsthatconnectedpeopleacrossgenerations.Theywerehowmemorieslivedonandhowvalueswerepasseddown.Everytimehetoldatale,hefelthewasaddingsomethinggoodtotheworld—alittlewarmth,alittlewonder.Astheyearswenton,Eliasgr";
    memcpy(bigstringinheap + 3000, biglev, 1000);
    biglev = "ewslower.Hisstepsweresmaller,hisvoicequieter,buthestillsatonthatsamebench,stilltoldthosesamestories.Thechildrenwhooncesatcross-leggedathisfeetgrewintoadultswithstoriesoftheirown,buttheyneverforgotEliasorthefoxnamedAlric.Eventually,Eliaspassedon.Thevillagemourned,buthislegacyremained.Inthetownsquare,theyplacedacarvedwoodenbenchinhishonor,andonit,theyinscribedthewords:ToElias,thestoryteller,whosetaleslittheheartsofmany.Thechildrennowgrowntoldhisstoriestotheirchildren,whotoldthemtotheirs.Yearsturnedtodecades,butthestorieslivedon.Andifyoueverfindyourselfinthatquietvalley,andyousitonthatbenchbeneaththeoldoaktree,youjustmighthearthewindwhisperingthetaleofAlricthefox—clever,kind,andalwaysreadytohelp.Inaworldthatoftenfeelstoofast,tooloud,toofilledwithnoise,storieslikethoseofEliasandAlricremindustoslowdownandlisten.Theyremindusthatkindnessmatters,thathelpingothersisworthit,andthatimaginationisapowerfulthing.Sotakeamoment,whereveryouare,andremember:thereismagicinwords,andthereisbeautyintellin";
    memcpy(bigstringinheap + 4000, biglev, 1000);
    len = 5000;
    // char read_buf[len];


    char * otherbigstringinheap = alloc_phys_pages(3);
    iowriteat(my_file2, 80000, bigstringinheap, len);

    ioreadat(my_file2, 80000, otherbigstringinheap, len);

    for(int i = 0; i < len; i++){
        kprintf("%c", otherbigstringinheap[i]);
    }
    kprintf("\n");


    kprintf("New Block id:%d \n", ktfs_get_new_block());


    fscreate("file3");
    fscreate("file4");
    fscreate("file5");
    fscreate("file6");
    fscreate("file7");

    kprintf("fsopen val: %d \n", fsopen("file3", &my_file3));
    kprintf("fsopen val: %d \n", fsopen("file4", &my_file4));
    kprintf("fsopen val: %d \n", fsopen("file5", &my_file5));
    kprintf("fsopen val: %d \n", fsopen("file6", &my_file6));
    kprintf("fsopen val: %d \n", fsopen("file7", &my_file7));


    new_len = 3000;
    ioctl(my_file3, 3, &new_len);

    new_len = 4000;
    ioctl(my_file4, 3, &new_len);

    new_len = 5000;
    ioctl(my_file5, 3, &new_len);

    new_len = 6000;
    ioctl(my_file6, 3, &new_len);

    new_len = 70000;
    ioctl(my_file7, 3, &new_len);

    len = 16;

    string_write2 = "Write to file 3\0";

    kprintf("Wrote: %d \n", iowriteat(my_file3, 214, string_write2, len));

    kprintf("Read: %d \n", ioreadat(my_file3, 214, string_read, len));

    for(int i = 0; i < len; i++){
        kprintf("%c", string_read[i]);
    }
    kprintf("\n");


    string_write2 = "Write to file 4\0";

    iowriteat(my_file4, 1000, string_write2, len);

    ioreadat(my_file4, 1000, string_read, len);

    for(int i = 0; i < len; i++){
        kprintf("%c", string_read[i]);
    }
    kprintf("\n");

    string_write2 = "Write to file 5\0";

    iowriteat(my_file5, 1000, string_write2, len);

    ioreadat(my_file5, 1000, string_read, len);

    for(int i = 0; i < len; i++){
        kprintf("%c", string_read[i]);
    }
    kprintf("\n");


    string_write2 = "Write to file 6\0";

    iowriteat(my_file6, 1000, string_write2, len);

    ioreadat(my_file6, 1000, string_read, len);

    for(int i = 0; i < len; i++){
        kprintf("%c", string_read[i]);
    }
    kprintf("\n");

    kprintf("FS delete3 Val: %d \n", fsdelete("file3"));
    kprintf("FS delete4 Val: %d \n", fsdelete("file4"));
    kprintf("FS delete5 Val: %d \n", fsdelete("file5"));
    kprintf("FS delete6 Val: %d \n", fsdelete("file6"));
    //kprintf("FS delete7 Val: %d \n", fsdelete("file7"));

    //KTFS write test

    uint8_t arr[512];
    uint8_t buf[512];
    uint8_t num_blocks = 100;
    kprintf ("WRITE KTFS\n");
    for (uint8_t i = 0; i < num_blocks; i++) {
        //kprintf ("block %d\n", i);
        for (int j = 0; j < 512; j++) {
            arr[j] = i;
            //kprintf ("%d ", arr[j]);
        }
            //kprintf ("\n");

        iowriteat(my_file7, i * 512 , arr, 512);

    }

    fsflush();

    kprintf ("READ KTFS\n");
    for (uint8_t i = 0; i < num_blocks; i++) {
        //kprintf ("block %d\n", i);
        ioreadat(my_file7, i * 512, buf, 512);
        //cache_read_at(cache, i * 512, buf, 512);
        for (int j = 0; j < 512; j++) {
            assert(buf[j] == i);
            //kprintf ("%d ", buf[j]);
    }
   // kprintf ("\n");
    }

    int blkno = 10;
    int blkoff = 500;
    len = 8;

    //cache_write_at(cache, blkno * 512 + blkoff, buf, len);
    iowriteat(my_file7, blkno * 512 + blkoff, buf, len);
    uint8_t buf2[512];

    //cache_read_at(cache, blkno * 512, buf2, 512);
    ioreadat(my_file7, blkno * 512, buf2, 512);
    kprintf("\n");
    for (int j = 0; j < 512; j++) {
        kprintf ("%d ", buf2[j]);
    }
    kprintf("\n");
    kprintf ("ktfs test passed\n");





}

void test_open_files(){
    //unsigned long long len;
    //unsigned long long new_len;
    unsigned long long files = 96;
    int result;
    kprintf("hello \n");

    struct io * blkio;

    result = open_device("vioblk", 0, &blkio);
    if (result < 0) {
        kprintf("Error: %d\n", result);
        panic("Failed to open vioblk\n");
    }

    struct io *file_list[files];
    char names [files];

    for(int i = 33; i < 120; i++)
        names[i] = (char) i;


    for(int i = 0; i < 50; i++){
        fscreate(&names[i]);
    }

    for(int i = 0; i < 50; i++){
        assert(fsopen(&names[i], &file_list[i]) == 0);
    }



}
