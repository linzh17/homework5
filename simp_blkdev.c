
#include<linux/module.h>
#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/fs.h>
#include<linux/genhd.h>
#include<linux/blkdev.h>
#include<linux/version.h>

MODULE_LICENSE ("Dual BSD/GPL");
MODULE_AUTHOR("linzhihao");
MODULE_DESCRIPTION("For test");

#define SIMP_BLKDEV_DEVICEMAJOR        COMPAQ_SMART2_MAJOR2
#define SIMP_BLKDEV_DISKNAME        "simp_blkdev"
#define SIMP_BLKDEV_BYTES        (16*1024*1024)
#define SIMP_BLKDEV_MAXPARTITIONS      (64)

static struct request_queue *simp_blkdev_queue;
static struct gendisk *simp_blkdev_disk;
static int major_num = 0;
unsigned char simp_blkdev_data[SIMP_BLKDEV_BYTES];

static unsigned int simp_blkdev_make_request(struct request_queue *q, struct bio *bio)
{
        struct bio_vec bvec;
        struct bvec_iter i;
        void *dsk_mem;

        if ((bio->bi_iter.bi_sector << 9) + bio->bi_iter.bi_size > SIMP_BLKDEV_BYTES) {
                printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                        ": bad request: block=%llu, count=%u\n",
                        (unsigned long long)bio->bi_iter.bi_sector, bio->bi_iter.bi_size);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
                bio_endio(bio, 0, -EIO);
#else
                bio_endio(bio);
#endif
                return 0;
        }

        dsk_mem = simp_blkdev_data + (bio->bi_iter.bi_sector << 9);

        bio_for_each_segment(bvec, bio, i) {
                void *iovec_mem;

                switch (bio_data_dir(bio)) {
                case READ:
                        iovec_mem = kmap(bvec.bv_page) + bvec.bv_offset;
                        memcpy(iovec_mem, dsk_mem, bvec.bv_len);
                        kunmap(bvec.bv_page);
                        break;
                case WRITE:
                        iovec_mem = kmap(bvec.bv_page) + bvec.bv_offset;
                        memcpy(dsk_mem, iovec_mem, bvec.bv_len);
                        kunmap(bvec.bv_page);
                        break;
                default:
                        printk(KERN_ERR SIMP_BLKDEV_DISKNAME
                                ": unknown value of bio_rw: %lu\n",
                                bio_data_dir(bio));
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
                        bio_endio(bio, 0, -EIO);
#else
                        bio_endio(bio);
#endif
                        return 0;
                }
                dsk_mem += bvec.bv_len;
        }

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
        bio_endio(bio, bio->bi_iter.bi_size, 0);
#else
        bio_endio(bio);
#endif

        return 0;
}
/*static void simp_blkdev_do_request(struct request_queue *q)
 * {
 *         struct request *req;
 *                 while ((req = blk_fetch_request(q)) != NULL) {
 *if (((blk_rq_pos(req) + blk_rq_cur_sectors(req))*512)
 * > SIMP_BLKDEV_BYTES) {
 *printk(KERN_ERR SIMP_BLKDEV_DISKNAME
 *": bad request: block=%llu, count=%u\n",
 *(unsigned long long)blk_rq_pos(req),
 *                                                                                                                                                                                 blk_rq_cur_sectors(req));
 *                                                                                                                                                                                                         blk_end_request_all(req, -EIO);
 *                                                                                                                                                                                                                                 continue;
 *                                                                                                                                                                                                                                                 } 
 *
 *                                                                                                                                                                                                                                                                 switch (rq_data_dir(req)) { 
 *                                                                                                                                                                                                                                                                                 case READ:
 *                                                                                                                                                                                                                                                                                                         memcpy(bio_data(req->bio),
 *                                                                                                                                                                                                                                                                                                                                         simp_blkdev_data +( blk_rq_pos(req)*512),
 *                                                                                                                                                                                                                                                                                                                                                                         blk_rq_cur_sectors(req)*512 );
 *                                                                                                                                                                                                                                                                                                                                                                                                 blk_end_request_all(req, 1);
 *                                                                                                                                                                                                                                                                                                                                                                                                                         break;
 *                                                                                                                                                                                                                                                                                                                                                                                                                                         case WRITE:
 *                                                                                                                                                                                                                                                                                                                                                                                                                                                                 memcpy(simp_blkdev_data + (blk_rq_pos(req)*512),
 *                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 bio_data(req->bio), blk_rq_cur_sectors(req)*512); 
 *                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         blk_end_request_all(req, 1);
 *                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 break;
 *default:
 * No default because rq_data_dir(req) is 1 bit */ /*
                        break;
                }
        }
}*/ //第三章去除


struct block_device_operations simp_blkdev_fops = {
        .owner                = THIS_MODULE,
};

static int __init simp_blkdev_init(void)
{
        int ret;

        simp_blkdev_queue = blk_alloc_queue(GFP_KERNEL);
        if (!simp_blkdev_queue) {
                ret = -ENOMEM;
                goto err_alloc_queue;
        }
		blk_queue_make_request(simp_blkdev_queue, simp_blkdev_make_request);
        simp_blkdev_disk = alloc_disk(SIMP_BLKDEV_MAXPARTITIONS); 
        if (!simp_blkdev_disk) {
                ret = -ENOMEM;
                goto err_alloc_disk;
        }

        /*major_num = register_blkdev(major_num, "sbd");
 *                 if (major_num < 0) {
 *                                         printk(KERN_WARNING "sbd: unable to get major number\n");
 *                                                                 goto err_alloc_disk;
 *                                                                                 }*/

        strcpy(simp_blkdev_disk->disk_name, SIMP_BLKDEV_DISKNAME);
        simp_blkdev_disk->major = SIMP_BLKDEV_DEVICEMAJOR;
        simp_blkdev_disk->first_minor = 0;
        simp_blkdev_disk->fops = &simp_blkdev_fops;
        simp_blkdev_disk->queue = simp_blkdev_queue;
        set_capacity(simp_blkdev_disk, SIMP_BLKDEV_BYTES/512);
        add_disk(simp_blkdev_disk);

        return 0;
err_alloc_disk:
        blk_cleanup_queue(simp_blkdev_queue);
err_alloc_queue:
        return ret;
}

static void __exit simp_blkdev_exit(void)
{
        del_gendisk(simp_blkdev_disk);
        put_disk(simp_blkdev_disk);
       // unregister_blkdev(major_num, "sbd");
        blk_cleanup_queue(simp_blkdev_queue);
       
 }
       
                module_init(simp_blkdev_init);
                module_exit(simp_blkdev_exit);
