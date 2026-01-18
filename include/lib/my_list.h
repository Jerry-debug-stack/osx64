#ifndef OS_LIST_H
#define OS_LIST_H

#include <stddef.h>  // 用于 offsetof

/* 链表节点结构 */
typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head_t;

/* container_of 宏 - 核心魔法 */
#define container_of(ptr, type, member) ({          \
    const typeof(((type *)0)->member) *__mptr = (ptr);    \
    (type *)((char *)__mptr - offsetof(type, member));     \
})

/* offsetof 宏（如果编译器没有提供） */
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

/* 初始化链表头 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }

/* 定义并初始化链表头 */
#define LIST_HEAD(name) \
    list_head_t name = LIST_HEAD_INIT(name)

/* 初始化链表节点 */
static inline void INIT_LIST_HEAD(list_head_t *list)
{
    list->next = list;
    list->prev = list;
}

/* 链表内部连接函数 */
static inline void __list_add(list_head_t *new_,
                              list_head_t *prev,
                              list_head_t *next)
{
    next->prev = new_;
    new_->next = next;
    new_->prev = prev;
    prev->next = new_;
}

/* 头插法：在head后添加新节点 */
static inline void list_add(list_head_t *new_, list_head_t *head)
{
    __list_add(new_, head, head->next);
}

/* 尾插法：在head前添加新节点（添加到链表尾部） */
static inline void list_add_tail(list_head_t *new_, list_head_t *head)
{
    __list_add(new_, head->prev, head);
}

/* 链表内部删除函数 */
static inline void __list_del(list_head_t *prev, list_head_t *next)
{
    next->prev = prev;
    prev->next = next;
}

/* 删除节点 */
static inline void list_del(list_head_t *entry)
{
    __list_del(entry->prev, entry->next);
}

/* 删除节点并重新初始化（使其成为孤立的节点） */
static inline void list_del_init(list_head_t *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

/* 替换节点 */
static inline void list_replace(list_head_t *old,
                                list_head_t *new_)
{
    new_->next = old->next;
    new_->next->prev = new_;
    new_->prev = old->prev;
    new_->prev->next = new_;
}

/* 替换节点并重新初始化旧节点 */
static inline void list_replace_init(list_head_t *old,
                                     list_head_t *new_)
{
    list_replace(old, new_);
    INIT_LIST_HEAD(old);
}

/* 将节点从一个链表移动到另一个链表头部 */
static inline void list_move(list_head_t *list, list_head_t *head)
{
    __list_del(list->prev, list->next);
    list_add(list, head);
}

/* 将节点从一个链表移动到另一个链表尾部 */
static inline void list_move_tail(list_head_t *list, list_head_t *head)
{
    __list_del(list->prev, list->next);
    list_add_tail(list, head);
}

/* 判断链表是否为空 */
static inline int list_empty(const list_head_t *head)
{
    return head->next == head;
}

/* 判断链表是否只有一个节点 */
static inline int list_is_singular(const list_head_t *head)
{
    return !list_empty(head) && (head->next == head->prev);
}

/* 旋转链表：将第一个节点移到末尾 */
static inline void list_rotate_left(list_head_t *head)
{
    if (!list_empty(head))
        list_move_tail(head->next, head);
}

/* 获取链表第一个节点 */
static inline list_head_t *list_first(list_head_t *head)
{
    return list_empty(head) ? NULL : head->next;
}

/* 获取链表最后一个节点 */
static inline list_head_t *list_last(list_head_t *head)
{
    return list_empty(head) ? NULL : head->prev;
}

/* 获取链表长度（遍历计数） */
static inline int list_len(const list_head_t *head)
{
    int count = 0;
    const list_head_t *pos;
    
    for (pos = head->next; pos != head; pos = pos->next)
        count++;
    return count;
}

/* 将链表分成两部分，second成为新链表的头 */
static inline void list_split(list_head_t *first,
                              list_head_t *second,
                              list_head_t *head)
{
    if (first == head) {
        INIT_LIST_HEAD(second);
    } else {
        second->prev = head->prev;
        second->prev->next = second;
        second->next = first;
        first->prev->next = head;
        head->prev = first->prev;
        first->prev = second;
    }
}

/* 合并两个链表 */
static inline void list_splice(list_head_t *list,
                               list_head_t *head)
{
    if (!list_empty(list)) {
        list_head_t *first = list->next;
        list_head_t *last = list->prev;
        
        first->prev = head;
        last->next = head->next;
        head->next->prev = last;
        head->next = first;
    }
}

/* 合并两个链表，并清空源链表 */
static inline void list_splice_init(list_head_t *list,
                                    list_head_t *head)
{
    list_splice(list, head);
    INIT_LIST_HEAD(list);
}

/* ====================== 遍历宏 ====================== */

/* 基本遍历：遍历链表节点 */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/* 反向遍历 */
#define list_for_each_reverse(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)

/* 安全遍历：允许在遍历时删除节点 */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

/* 安全反向遍历 */
#define list_for_each_reverse_safe(pos, n, head) \
    for (pos = (head)->prev, n = pos->prev; pos != (head); \
         pos = n, n = pos->prev)

/* 遍历包含链表节点的结构体 */
#define list_for_each_entry(pos, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = container_of(pos->member.next, typeof(*pos), member))

/* 反向遍历包含链表节点的结构体 */
#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = container_of((head)->prev, typeof(*pos), member); \
         &pos->member != (head); \
         pos = container_of(pos->member.prev, typeof(*pos), member))

/* 安全遍历包含链表节点的结构体 */
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member), \
         n = container_of(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, n = container_of(n->member.next, typeof(*n), member))

/* 安全反向遍历包含链表节点的结构体 */
#define list_for_each_entry_reverse_safe(pos, n, head, member) \
    for (pos = container_of((head)->prev, typeof(*pos), member), \
         n = container_of(pos->member.prev, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, n = container_of(n->member.prev, typeof(*n), member))

/* 获取第一个entry */
#define list_first_entry(ptr, type, member) \
    container_of((ptr)->next, type, member)

/* 获取最后一个entry */
#define list_last_entry(ptr, type, member) \
    container_of((ptr)->prev, type, member)

/* 遍历链表时获取下一个entry（用于在循环内continue后继续遍历） */
#define list_next_entry(pos, member) \
    container_of((pos)->member.next, typeof(*(pos)), member)

/* 遍历链表时获取上一个entry */
#define list_prev_entry(pos, member) \
    container_of((pos)->member.prev, typeof(*(pos)), member)


#endif
