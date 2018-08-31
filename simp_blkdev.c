
#include<linux/module.h>
#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/fs.h>
#include<linux/genhd.h>
#include<linux/blkdev.h>
#include<linux/version.h>
#include <linux/hdreg.h>

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
unsigned char simp_blkdev_data[SIMP_BLKDEV_BYTES]; //第四章修改 第六章去除
static struct radix_tree_root simp_blkdev_data;

int alloc_diskmem(void)
{
        int ret;
        int i;
        void *p;

        INIT_RADIX_TREE(&simp_blkdev_data, GFP_KERNEL);

        for (i = 0; i < (SIMP_BLKDEV_BYTES + PAGE_SIZE - 1) >> PAGE_SHIFT;
                i++) {
                p = (void *)__get_free_page(GFP_KERNEL);
                if (!p) {
                        ret = -ENOMEM;
                        goto err_alloc;
                }

                ret = radix_tree_insert(&simp_blkdev_data, i, p);
                if (IS_ERR_VALUE(ret))
                        goto err_radix_tree_insert;
        }
        return 0;

err_radix_tree_insert:
        free_page((unsigned long)p);
err_alloc:
        free_diskmem();
        return ret;
} //第六章增加

void free_diskmem(void)
{
        int i;
        void *p;

        for (i = 0; i < (SIMP_BLKDEV_BYTES + PAGE_SIZE - 1) >> PAGE_SHIFT;
                i++) {
                p = radix_tree_lookup(&simp_blkdev_data, i);
                radix_tree_delete(&simp_blkdev_data, i);
                /* free NULL is safe */
                free_page((unsigned long)p);
        }
}//第六章增加

static unsigned int simp_blkdev_make_request(struct request_queue *q, struct bio *bio)
{
        struct bio_vec bvec;
        struct bvec_iter i;
       // void *dsk_mem;
        unsigned long long dsk_offset;
        dsk_offset = bio->bi_iter.bi_sector * 512

       /* if ((bio->bi_iter.bi_sector << 9) + bio->bi_iter.bi_size > SIMP_BLKDEV_BYTES) {
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

        dsk_mem = simp_blkdev_data + (bio->bi_iter.bi_sector << 9); */

        bio_for_each_segment(bvec, bio, i) {
                unsigned int count_done, count_current;
                void *iovec_mem;
                void *dsk_mem;

                iovec_mem = kmap(bvec.bv_page) + bvec.bv_offset;

                count_done = 0;
               while (count_done < bvec.bv_len) {
                        count_current = min(bvec.bv_len - count_done, PAGE_SIZE - (dsk_offset + count_done) % PAGE_SIZE);

                        dsk_mem = radix_tree_lookup(&simp_blkdev_data, (dsk_offset + count_done) / PAGE_SIZE);
                        dsk_mem += (dsk_offset + count_done) % PAGE_SIZE;  

                switch (bio_data_dir(bio)) {
                        case READ:
                        case READA:
                                memcpy(iovec_mem + count_done, dsk_mem, count_current);
                                break;
                        case WRITE:
                                memcpy(dsk_mem, iovec_mem + count_done, count_current);
                                break;
                        }
                 count_done += count_current;
                }

                kunmap(bvec.bv_page);
                dsk_offset += bvec.bv_len;
            }    


        bio_endio(bio);
        return 0;
}

static int simp_blkdev_getgeo(struct block_device *bdev,
                struct hd_geometry *geo)
{
        /*
         * capacity        heads        sectors        cylinders
         * 0~16M        1        1        0~32768
         * 16M~512M        1        32        1024~32768
         * 512M~16G        32        32        1024~32768
         * 16G~...        255        63        2088~...
         */
        if (SIMP_BLKDEV_BYTES < 16 * 1024 * 1024) {
                geo->heads = 1;
                geo->sectors = 1;

        } else if (SIMP_BLKDEV_BYTES < 512 * 1024 * 1024) {
                geo->heads = 1;
                geo->sectors = 32;
        } else if (SIMP_BLKDEV_BYTES < 16ULL * 1024 * 1024 * 1024) {
                geo->heads = 32;
                geo->sectors = 32;
        } else {
                geo->heads = 255;
                geo->sectors = 63;
        }

        geo->cylinders = SIMP_BLKDEV_BYTES>>9/geo->heads/geo->sectors;

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

         ret = alloc_diskmem();
        if (IS_ERR_VALUE(ret))
                goto err_alloc_diskmem; // 第六章增加

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

err_alloc_diskmem:
        put_disk(simp_blkdev_disk); // 第六章增加
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
