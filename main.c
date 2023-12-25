#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_CLUSTERS 4085
#define MIN_FAT_LENGTH (MIN_CLUSTERS + 2)

#if __BYTE_ORDER != __LITTLE_ENDIAN
#error FAT12CONV only supports little-endian platforms
#endif

static FILE *try_open(const char *path, const char *mode) {
    errno = 0;
    FILE *f = fopen(path, mode);

    if (f == NULL) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    return f;
}

static void try_read(void *buf, size_t size, size_t num, FILE *file) {
    errno = 0;
    if (fread(buf, size, num, file) != num) {
        fprintf(stderr, "read failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void try_write(const void *buf, size_t size, size_t num, FILE *file) {
    errno = 0;
    if (fwrite(buf, size, num, file) != num) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

typedef struct {
    uint8_t jump_code[3];
    uint8_t oem_id[8];
    uint16_t sector_size;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fats;
    uint16_t root_dir_slots;
    uint16_t small_size;
    uint8_t media_descriptor;
    uint16_t fat_size;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t large_size;
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t signature;
    uint32_t serial;
    uint8_t label[11];
    uint8_t type[8];
} __attribute__((packed)) bpb_t;

static void discard(FILE *in, uint32_t size, uint8_t *buf, size_t buf_size) {
    while (size > 0) {
        uint32_t read_size = size < buf_size ? size : buf_size;

        try_read(buf, 1, read_size, in);
        size -= read_size;
    }
}

static void write0(FILE *out, uint32_t size) {
    static const uint8_t zero[4096];

    uint32_t done = 0;
    while (done < size) {
        uint32_t write_size = size - done;
        if (write_size > sizeof(zero)) write_size = sizeof(zero);

        try_write(zero, 1, write_size, out);
        done += write_size;
    }
}

static void copy(FILE *in, FILE *out, uint32_t size) {
    uint8_t buffer[4096];
    uint32_t pos = 0;

    while (pos < size) {
        uint32_t read_size = size - pos;
        if (read_size > sizeof(buffer)) read_size = sizeof(buffer);

        try_read(buffer, 1, read_size, in);
        try_write(buffer, 1, read_size, out);

        pos += read_size;
    }
}

static void write_u16(FILE *out, uint16_t value) {
    try_write(&value, 2, 1, out);
}

static uint16_t map_entry(uint16_t entry) {
    return entry >= 0xff7 ? (0xf000 | entry) : entry;
}

static void copy_fat(FILE *in, FILE *out, bpb_t *bpb, uint32_t in_fat_size) {
    uint8_t buffer[4096];

    // skip first 2 entries from input
    try_read(buffer, 1, 3, in);

    // write first 2 entries
    write_u16(out, bpb->media_descriptor | (bpb->media_descriptor & 0x80 ? 0xff00 : 0));
    write_u16(out, 0xffff);

    // copy entries
    uint32_t size = 4;
    uint32_t i;
    for (i = 3; i + 3 <= in_fat_size; i += 3) {
        try_read(buffer, 1, 3, in);
        uint16_t entry1 = map_entry(buffer[0] | ((buffer[1] & 0xf) << 8));
        uint16_t entry2 = map_entry(((buffer[1] & 0xf0) >> 4) | (buffer[2] << 4));

        write_u16(out, entry1);
        write_u16(out, entry2);
        size += 4;
    }

    // discard input until fat is completely read
    discard(in, in_fat_size - i, buffer, sizeof(buffer));

    // fill remaining space with zeroes
    write0(out, bpb->fat_size * bpb->sector_size - size);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT OUTPUT\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *in = try_open(argv[1], "rb");
    FILE *out = try_open(argv[2], "wb");

    bpb_t bpb;
    try_read(&bpb, sizeof(bpb), 1, in);

    uint32_t volume_size = bpb.small_size ? bpb.small_size : bpb.large_size;
    uint32_t root_dir_size = (bpb.root_dir_slots * 32 + bpb.sector_size - 1) / bpb.sector_size;
    uint32_t data_start = bpb.reserved_sectors + bpb.fats * bpb.fat_size + root_dir_size;
    uint32_t data_size = volume_size - data_start;
    uint32_t clusters = data_size / bpb.sectors_per_cluster;

    if (clusters >= MIN_CLUSTERS) {
        fprintf(stderr, "%s: already FAT16\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint32_t in_fat_size = bpb.fat_size;
    uint32_t min_fat_size = (MIN_FAT_LENGTH * 2 + bpb.sector_size - 1) / bpb.sector_size;
    if (min_fat_size > bpb.fat_size) {
        bpb.fat_size = min_fat_size;
    }

    uint32_t new_data_start = bpb.reserved_sectors + bpb.fats * bpb.fat_size + root_dir_size;
    uint32_t min_data_size = MIN_CLUSTERS * bpb.sectors_per_cluster;
    uint32_t min_volume_size = new_data_start + min_data_size;
    if (min_volume_size > volume_size) {
        if (min_volume_size > 0xffff) {
            bpb.small_size = 0;
            bpb.large_size = min_volume_size;
        } else {
            bpb.small_size = min_volume_size;
            bpb.large_size = 0;
        }
    }

    memcpy(bpb.type, "FAT16   ", sizeof(bpb.type));

    // write new bpb and copy reserved sectors
    try_write(&bpb, sizeof(bpb), 1, out);
    copy(in, out, bpb.reserved_sectors * bpb.sector_size - sizeof(bpb));

    // copy FATs
    for (unsigned i = 0; i < bpb.fats; i++) {
        copy_fat(in, out, &bpb, in_fat_size * bpb.sector_size);
    }

    // copy root directory
    copy(in, out, root_dir_size * bpb.sector_size);

    // copy data
    copy(in, out, data_size * bpb.sector_size);

    // extend volume
    if (min_data_size > data_size) {
        write0(out, (min_data_size - data_size) * bpb.sector_size);
    }

    fclose(out);
    fclose(in);
    return EXIT_SUCCESS;
}
