/*
 * Virtio device common utilities
 *
 * Copyright (C) 2026 Francesco Valla
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _DEV_VIRTIO_COMMON_H_
#define _DEV_VIRTIO_COMMON_H_

#if defined(CONFIG_OPENAMP_WITH_DCACHE)
#define BUFFER_FLUSH(x, s)		metal_cache_flush(x, s)
#define BUFFER_INVALIDATE(x, s)		metal_cache_invalidate(x, s)
#else
#define BUFFER_FLUSH(x, s)		do { } while (0)
#define BUFFER_INVALIDATE(x, s)		do { } while (0)
#endif /* OPENAMP_WITH_DCACHE */

#endif	/* _DEV_VIRTIO_COMMON_H_ */
