
#include<linux/module.h>
#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/fs.h>
#include<linux/genhd.h>
#include<linux/blkdev.h>
#include<linux/version.h>
#include <linux/hdreg.h>
//edit at 10th

MODULE_LICENSE ("Dual BSD/GPL");
MODULE_AUTHOR("linzhihao");
MODULE_DESCRIPTION("For test");


#define SIMP_BLKDEV_DATASEGORDER        (2)  // order=2 每次从伙伴系统中申请连续的4个页面
#define SIMP_BLKDEV_DATASEGSHIFT        (PAGE_SHIFT + SIMP_BLKDEV_DATASEGORDER)  //偏移量和内存段之间相互转换时使用的移位值
#define SIMP_BLKDEV_DATASEGSIZE                (PAGE_SIZE << SIMP_BLKDEV_DATASEGORDER) //字节为单位的内存段的长度 16384
#define SIMP_BLKDEV_DATASEGMASK                (~(SIMP_BLKDEV_DATASEGSIZE-1)) //内存段的屏蔽位

#define SIMP_BLKDEV_DEVICEMAJOR        COMPAQ_SMART2_MAJOR2
#define SIMP_BLKDEV_DISKNAME        "simp_blkdev"
//#define simp_blkdev_bytes        (16*1024*1024)
#define SIMP_BLKDEV_MAXPARTITIONS      (64)

#define SIMP_BLKDEV_SECTORSHIFT        (9)
#define SIMP_BLKDEV_SECTORSIZE        (1ULL<<SIMP_BLKDEV_SECTORSHIFT)
#define SIMP_BLKDEV_SECTORMASK        (~(SIMP_BLKDEV_SECTORSIZE-1))

static char *simp_blkdev_param_size = "16M"; //磁盘大小的默认值指定为16M
module_param_named(size, simp_blkdev_param_size, charp, S_IRUGO);//允许用户在模块加载后改变磁盘大小

static unsigned long long simp_blkdev_bytes;

static struct request_queue *simp_blkdev_queue;
static struct gendisk *simp_blkdev_disk;
//static int major_num = 0;
//unsigned char simp_blkdev_data[simp_blkdev_bytes]; //第四章修改 第六章去除
static struct radix_tree_root simp_blkdev_data;
DEFINE_MUTEX(simp_blkdev_datalock); /* protects the disk data op */ //锁simp_blkdev_data


void free_diskmem(void)
{
        unsigned long long next_seg;
        struct page *seglist[64];
        int listcnt;
        int i;
        //void *p;
        // struct page *page;
        next_seg = 0;
        do {
                listcnt = radix_tree_gang_lookup(&simp_blkdev_data,
                        (void **)seglist, next_seg, ARRAY_SIZE(seglist));//获得了一组需要释放的指针

                for (i = 0; i < listcnt; i++) {
                        next_seg = seglist[i]->index;
                        radix_tree_delete(&simp_blkdev_data, next_seg);
                        __free_pages(seglist[i], SIMP_BLKDEV_DATASEGORDER);
                }

                next_seg++;
        } while (listcnt == ARRAY_SIZE(seglist));
}

/*int alloc_diskmem(void)
{........
}*/ //十四章删除

static int simp_blkdev_trans_oneseg(struct page *start_page, unsigned long offset, void *buf, unsigned int len, int dir)
{
        unsigned int done_cnt;
        struct page *this_page;
        unsigned int this_off;
        unsigned int this_cnt;
        void *dsk_mem;

        done_cnt = 0;
        while (done_cnt < len) {
                /* iterate each page */
                this_page = start_page + ((offset + done_cnt) >> PAGE_SHIFT);
                this_off = (offset + done_cnt) & ~PAGE_MASK;
                this_cnt = min(len - done_cnt, (unsigned int)PAGE_SIZE
                        - this_off);

                dsk_mem = kmap(this_page);
                if (!dsk_mem) {
                        printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                                ": map device page failed: %p\n", this_page);
                        return -ENOMEM;
                }
                dsk_mem += this_off;

                if (!dir)
                        memcpy(buf + done_cnt, dsk_mem, this_cnt);
                else
                        memcpy(dsk_mem, buf + done_cnt, this_cnt);

                kunmap(this_page);

                done_cnt += this_cnt;
        }

        return 0;
}//page指针对应的内存，然后根据给定的数据方向执行指定长度的数据传输。

static int simp_blkdev_trans(unsigned long long dsk_offset, void *buf,unsigned int len, int dir)//处理块设备与一段连续内存之间数据传输
{
        unsigned int done_cnt;//记录前几次中已经完成的数据量
        struct page *this_first_page;
        unsigned int this_off;
        unsigned int this_cnt;

        done_cnt = 0;
        while (done_cnt < len) {
                /* iterate each data segment */
                this_off = (dsk_offset + done_cnt) & ~SIMP_BLKDEV_DATASEGMASK;//块设备偏移所在位置到页头
                this_cnt = min(len - done_cnt,(unsigned int)SIMP_BLKDEV_DATASEGSIZE - this_off);//确定这一次传送的数据量
                mutex_lock(&simp_blkdev_datalock);//锁simp_blkdev_data
                //找出块设备数据块的第一个page 查找基数中的一个内存块
                this_first_page = radix_tree_lookup(&simp_blkdev_data,(dsk_offset + done_cnt) >> SIMP_BLKDEV_DATASEGSHIFT);

                if (!this_first_page) {
                        if (!dir) {
                                memset(buf + done_cnt, 0, this_cnt);
                                goto trans_done;
                        }

                        /* prepare new memory segment for write */
                        this_first_page = alloc_pages(
                                GFP_KERNEL | __GFP_ZERO | __GFP_HIGHMEM,
                                SIMP_BLKDEV_DATASEGORDER);
                        if (!this_first_page) {
                                printk(KERN_ERR SIMP_BLKDEV_DISKNAME": allocate page failed\n");
                                        mutex_unlock(&simp_blkdev_datalock);
                                return -ENOMEM;
                        }
                        //基树的索引存入page.index
                        this_first_page->index = (dsk_offset + done_cnt) >> SIMP_BLKDEV_DATASEGSHIFT;

                 if (IS_ERR_VALUE(radix_tree_insert(&simp_blkdev_data,this_first_page->index, this_first_page))) {
                printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                        ": insert page to radix_tree failed"
                        " seg=%lu\n", this_first_page->index);
                __free_pages(this_first_page,SIMP_BLKDEV_DATASEGORDER);
                mutex_unlock(&simp_blkdev_datalock);//把锁还回去
                return -EIO;
                }
        }

        if (IS_ERR_VALUE(simp_blkdev_trans_oneseg(this_first_page,this_off, buf + done_cnt, this_cnt, dir))){
                        
                        mutex_unlock(&simp_blkdev_datalock);
                        return -EIO;
        }

trans_done:
        mutex_unlock(&simp_blkdev_datalock);
        done_cnt += this_cnt;               
        }


        return 0;
}//分割请求数据搞定了数据跨越多个块设备数据块的问题，并且顺便把块设备数据块的第一个page给找了出来



static unsigned int simp_blkdev_make_request(struct request_queue *q, struct bio *bio)//负责决定数据传输方向、检查bio请求是否合法、遍历bio中的每个bvec、映射bvec中的内存页，
{
        int dir;
        struct bio_vec bvec;
        struct bvec_iter i;
       // void *dsk_mem;
        unsigned long long dsk_offset;
        void *iovec_mem;

        //数据传输方向
        switch (bio_data_dir(bio)) {
                case READ:
                //case READA:
                        dir = 0;
                        break;
                case WRITE:
                        dir = 1;
                        break;
                default:
                        printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                                ": unknown value of bio_data_dir: %lu\n", bio_data_dir(bio));
                        goto bio_err;
                }

        
        //检查bio请求是否合法
       if ((bio->bi_iter.bi_sector << SIMP_BLKDEV_SECTORSHIFT) + bio->bi_iter.bi_size > simp_blkdev_bytes) {
                printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                        ": bad request: block=%llu, count=%u\n",
                        (unsigned long long)bio->bi_iter.bi_sector, bio->bi_iter.bi_size);
                goto bio_err;
        } 

        dsk_offset = bio->bi_iter.bi_sector<<SIMP_BLKDEV_SECTORSHIFT;

        /*dsk_mem = simp_blkdev_data + (bio->bi_iter.bi_sector << 9); */

        bio_for_each_segment(bvec, bio, i) {
               // unsigned int count_done, count_current;
                //void *iovec_mem;
                //void *dsk_mem;
                //struct page *dsk_page;
                

                iovec_mem = kmap(bvec.bv_page) + bvec.bv_offset;//映射访问内存页面
                 if (!iovec_mem) {
                        printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                                ": map iovec page failed: %p\n", bvec.bv_page);
                        goto bio_err;
                }
                //数据与iovec_mem地址之间的传送
                 if (IS_ERR_VALUE(simp_blkdev_trans(dsk_offset, iovec_mem,
                        bvec.bv_len, dir)))
                        goto bio_err;

                kunmap(bvec.bv_page);//把映射的区域还回去

                dsk_offset += bvec.bv_len;//每处理完成一个bio_vec时，更新dsk_offset值
        }
        bio_endio(bio);
        return 0;
bio_err:
bio_endio(bio);
return 0;        
}

int getparam(void)//对模块参数进行解析，设置simp_blkdev_bytes变量的值
{
        char unit;
        char tailc;

        if (sscanf(simp_blkdev_param_size, "%llu%c%c", &simp_blkdev_bytes,
                &unit, &tailc) != 2) {
                return -EINVAL;
        }

        if (!simp_blkdev_bytes)
                return -EINVAL;

        //用字符串来指定大小
        switch (unit) {
        case 'g':
        case 'G':
                simp_blkdev_bytes <<= 30;
                break;
        case 'm':
        case 'M':
                simp_blkdev_bytes <<= 20;
                break;
        case 'k':
        case 'K':
                simp_blkdev_bytes <<= 10;
                break;
        case 'b':
        case 'B':
                break;
        default:
                return -EINVAL;
        }

        /* make simp_blkdev_bytes fits sector's size */
        simp_blkdev_bytes = (simp_blkdev_bytes + SIMP_BLKDEV_SECTORSIZE - 1) & SIMP_BLKDEV_SECTORMASK;

        return 0;
}

static int simp_blkdev_getgeo(struct block_device *bdev,
                struct hd_geometry *geo)//获得块设备物理结构 磁道的上限设置成32768
{
        /*
         * capacity        heads        sectors        cylinders
         * 0~16M        1        1        0~32768
         * 16M~512M        1        32        1024~32768
         * 512M~16G        32        32        1024~32768
         * 16G~...        255        63        2088~...
         */
        if (simp_blkdev_bytes < 16 * 1024 * 1024) {
                geo->heads = 1;   //磁头数
                geo->sectors = 1;//每磁道扇区数

        } else if (simp_blkdev_bytes < 512 * 1024 * 1024) {
                geo->heads = 1;
                geo->sectors = 32;
        } else if (simp_blkdev_bytes < 16ULL * 1024 * 1024 * 1024) {
                geo->heads = 32;
                geo->sectors = 32;
        } else {
                geo->heads = 255;
                geo->sectors = 63;
        }

        geo->cylinders = simp_blkdev_bytes>>SIMP_BLKDEV_SECTORSHIFT/geo->heads/geo->sectors;//计算磁道数

        return 0;
}//第五章增加
/*static void simp_blkdev_do_request(struct request_queue *q)
 *...........                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               break;
 *default:
 * No default because rq_data_dir(req) is 1 bit */ /*
                        break;
                }
        }
}*/ //第三章去除


struct block_device_operations simp_blkdev_fops = {
        .owner                = THIS_MODULE,
        .getgeo                = simp_blkdev_getgeo,
};

static int __init simp_blkdev_init(void)
{
        int ret;

        simp_blkdev_queue = blk_alloc_queue(GFP_KERNEL);//获取request_queue
        if (!simp_blkdev_queue) {
                ret = -ENOMEM;
                goto err_alloc_queue;
        }
	blk_queue_make_request(simp_blkdev_queue, simp_blkdev_make_request);
        simp_blkdev_disk = alloc_disk(SIMP_BLKDEV_MAXPARTITIONS); //申请添加的设备资源
        if (!simp_blkdev_disk) {
                ret = -ENOMEM;
                goto err_alloc_disk;
        }

         INIT_RADIX_TREE(&simp_blkdev_data, GFP_KERNEL);//十四章  初始化一个基树的结构
        //ret = alloc_diskmem();
        //if (IS_ERR_VALUE(ret))
        //        goto err_alloc_diskmem; // 第六章增加 十四章删除

        ret = getparam();
        if (IS_ERR_VALUE(ret))
                goto err_getparam;        

        /*major_num = register_blkdev(major_num, "sbd");
 *                 if (major_num < 0) {
 *                                         printk(KERN_WARNING "sbd: unable to get major number\n");
 *                                                                 goto err_alloc_disk;
 *                                                                                 }*/

        strcpy(simp_blkdev_disk->disk_name, SIMP_BLKDEV_DISKNAME);
        simp_blkdev_disk->major = SIMP_BLKDEV_DEVICEMAJOR;//每个设备需要对应的主、从驱动号
        simp_blkdev_disk->first_minor = 0;
        simp_blkdev_disk->fops = &simp_blkdev_fops;
        simp_blkdev_disk->queue = simp_blkdev_queue;//请求队列
        set_capacity(simp_blkdev_disk, simp_blkdev_bytes>>SIMP_BLKDEV_SECTORSHIFT);
        add_disk(simp_blkdev_disk);

        return 0;

 err_getparam:
        return ret;
//err_alloc_diskmem:
//        put_disk(simp_blkdev_disk); // 第六章增加  十四章删除
err_alloc_disk:
        blk_cleanup_queue(simp_blkdev_queue);
err_alloc_queue:
        return ret;
}

static void __exit simp_blkdev_exit(void)
{
        del_gendisk(simp_blkdev_disk);
        put_disk(simp_blkdev_disk);
        free_diskmem();
       // unregister_blkdev(major_num, "sbd");
        blk_cleanup_queue(simp_blkdev_queue);
       
 }
       
                module_init(simp_blkdev_init);
                module_exit(simp_blkdev_exit);
