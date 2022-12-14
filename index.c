#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "libhttpd.h"

struct httpd_var_list website;

static size_t
loop(struct httpd_var_list *vars, char *key, int (*fn)(char *path))
{
	char *val, list[1024];
	size_t n = 0;

	if ((val = httpd_get_var(vars, key)) == NULL)
		httpd_fatal("looping over $%s: no such variable", key);
	strlcpy(list, val, sizeof list);
	for (char *s = list, *ref = list; (s = strsep(&ref, " ")) != NULL;) {
		char path[64];

		if (*s == '\0')
			continue;
		snprintf(path, sizeof path, "db/%s/%s", key, s);
		if (fn(path) != 0)
			break;
		n++;
	}
	return n;
}

static int
loop_void(char *path)
{
	(void)path;
	return 0;
}

static ino_t
get_inode(char *path)
{
	struct stat st = {0};

	if (stat(path, &st) == -1)
		httpd_fatal("stat %s: %s", path, strerror(errno));
	return st.st_ino;
}

static void
add_id(char *parent, char *child, uint32_t id)
{
	struct httpd_var_list vars = {0};
	char new[1024], path[64], *val;

	snprintf(path, sizeof path, "db/%s", parent);
	httpd_read_var_list(&vars, path);
	if ((val = httpd_get_var(&vars, child)) == 0)
		httpd_fatal("adding id %d to %s: no $%s", parent, id, child);

	snprintf(new, sizeof new, (*val == '\0') ? "%s%d" : "%s %d", val, id);
	httpd_set_var(&vars, child, new);
	httpd_write_var_list(&vars, path);
	httpd_free_var_list(&vars);
}

static void
del_string(char *new, size_t sz, char *old, char *id)
{
	char *cp;
	size_t len = strlen(id);

	for (cp = old; (cp = strstr(cp, id));)
		if (cp[len] == ' ' || cp[len] == '\0')
			goto found;
	httpd_fatal("could not find '%s' to delete in '%s' in $%s", id, old);
found:
	len += (cp[len] == ' ');
	snprintf(new, sz, "%.*s%s", (int)(cp - old), old, cp + len);
	len = strlen(new);
	if (new[len] == ' ')
		new[len] = '\0';
}

static void
del_id(char *parent, char *child, char *id)
{
	struct httpd_var_list vars = {0};
	char path[64], *val, new[1024];

	snprintf(path, sizeof path, "db/%s", parent);
	httpd_read_var_list(&vars, path);
	if ((val = httpd_get_var(&vars, child)) == 0)
		httpd_fatal("removing id %d from %s: no $%s", parent, id, child);

	del_string(new, sizeof new, val, id);
	httpd_set_var(&vars, child, new);
	httpd_write_var_list(&vars, path);
}

static uint32_t
payload_assign_id(char *tmp, char *table)
{
	char path[64];
	uint32_t id;

	id = get_inode(tmp);
	snprintf(path, sizeof path, "db/%s/%d", table, id);
	if (rename(tmp, path) == -1)
		httpd_fatal("rename %s to %s: %s", tmp, path, strerror(errno));
	return id;
}

static void
payload_as_child(char *parent, char *child)
{
	struct httpd_var_list *payload = httpd_parse_payload();
	char tmp[64];

	snprintf(tmp, sizeof tmp, "tmp/%d", getpid());
	httpd_write_var_list(payload, tmp);
	add_id(parent, child, payload_assign_id(tmp, child));
}

static void
payload_as_file(char *parent, char *child)
{
	char tmp[64];

	snprintf(tmp, sizeof tmp, "tmp/%d", getpid());
	httpd_receive_file(tmp);
	add_id(parent, child, payload_assign_id(tmp, child));
}

static int
website_get_cart_count(void)
{
	struct httpd_var_list *cookies = httpd_parse_cookies();

	if (httpd_get_var(cookies, "item") != NULL)
		return loop(cookies, "item", loop_void);
	return 0;
}

static int
website_loop_nav_category(char *path)
{
	struct httpd_var_list vars = {0};

	httpd_read_var_list(&vars, path);
	httpd_template("html/website-nav-category.html", &vars);
	return 0;
}

static void
website_head(char *page_name)
{
	int cart_count = website_get_cart_count();

	httpd_read_var_list(&website, "db/website");
	httpd_set_var(&website, "page-name", page_name ? page_name : "(null)");
	httpd_send_headers(200, "text/html");

	httpd_template("html/website-head.html", &website);
	printf("<nav>\n");
	loop(&website, "category", website_loop_nav_category);
	printf("<a href=\"/cart/\" class=\"button right\">Panier");
	if (cart_count > 0)
		printf(" <span class=\"counter\">%d</span>", cart_count);
	printf("</a>\n");
	printf("</nav>\n");
	printf("<main>\n");
}

static void
website_foot(void)
{
	printf("</main>\n");
}

static void
error_404(char **matches)
{
	(void)matches;

	website_head("404");
	httpd_template("html/404.html", &website);
	printf("<code>");
	httpd_print_html(matches[0]);
	printf("</code>\n");
	website_foot();
}

static int
home_loop_image(char *path)
{
	struct httpd_var_list vars = {0};

	assert(strchr(path, '/'));
	httpd_set_var(&vars, "file", strrchr(path, '/') + 1);
	httpd_template("html/home-image.html", &vars);
	return -1;
}

static int
home_loop_item(char *path)
{
	struct httpd_var_list vars = {0};

	httpd_read_var_list(&vars, path);
	httpd_template("html/home-item-head.html", &vars);
	loop(&vars, "image", home_loop_image);
	httpd_template("html/home-item-foot.html", &vars);
	return 0;
}

static int
home_loop_category(char *path)
{
	struct httpd_var_list vars = {0};

	httpd_read_var_list(&vars, path);
	httpd_template("html/home-category-head.html", &vars);
	loop(&vars, "item", home_loop_item);
	httpd_template("html/home-category-foot.html", &vars);
	return 0;
}

static void
page_home(char **matches)
{
	(void)matches;

	website_head("Accueil");
	loop(&website, "category", home_loop_category);
	website_foot();
}

static int item_image_checked;

static int
item_loop_image(char *path)
{
	struct httpd_var_list vars = {0};

	if (item_image_checked)
		httpd_set_var(&vars, "checked", "checked");
	assert(strchr(path, '/'));
	httpd_set_var(&vars, "file", strrchr(path, '/') + 1);
	httpd_template("html/item-image.html", &vars);
	item_image_checked = 0;
	return 0;
}

static void
page_item(char **matches)
{
	struct httpd_var_list vars = {0};
	char path[64];

	snprintf(path, sizeof path, "db/item/%s", matches[0]);
	httpd_read_var_list(&vars, path);
	website_head(httpd_get_var(&vars, "item.name"));
	httpd_template("html/item-head.html", &vars);
	item_image_checked = 1;
	loop(&vars, "image", item_loop_image);
	httpd_template("html/item-foot.html", &vars);
	website_foot();
}

uint32_t cart_subtotal, cart_shipping, cart_total;

static int
cart_loop_item(char *path)
{
	struct httpd_var_list vars = {0};
	char const *val, *err = NULL;

	httpd_read_var_list(&vars, path);
	httpd_template("html/cart-item.html", &vars);

	if ((val = httpd_get_var(&vars, "item.price")) == NULL)
		httpd_fatal("could not get the price for '%s'", path);
	if ((cart_subtotal += strtonum(val, 0, UINT32_MAX, &err)), err != NULL)
		httpd_fatal("parsing price for '%s': %s", path, err);
	return 0;
}

static void
page_cart(char **matches)
{
	struct httpd_var_list *cookies = httpd_parse_cookies();
	struct httpd_var_list vars = {0};
	char *val, subtotal[32], shipping[32], total[32];
	char const *err;
	(void)matches;

	website_head("Panier");

	if ((val = httpd_get_var(cookies, "item")) == NULL || *val == '\0') {
		httpd_template("html/cart-empty.html", &website);
	} else {
		loop(cookies, "item", cart_loop_item);

		if ((val = httpd_get_var(&website, "cart.shipping")) == NULL)
			httpd_fatal("no $cart.shipping variable", err);
		cart_shipping = strtonum(val, 0, UINT32_MAX, &err);
		if (err != NULL)
			httpd_fatal("parsing shipping costs: %s", err);
		cart_total = cart_subtotal + cart_shipping;

		snprintf(subtotal, sizeof subtotal, "%d", cart_subtotal);
		httpd_set_var(&vars, "cart.subtotal", subtotal);

		snprintf(shipping, sizeof shipping, "%d", cart_shipping);
		httpd_set_var(&vars, "cart.shipping", shipping);

		snprintf(total, sizeof total, "%d", cart_total);
		httpd_set_var(&vars, "cart.total", total);

		httpd_set_var(&vars, "cart.items", httpd_get_var(cookies, "item"));

		httpd_template("html/cart-checkout.html", &vars);
	}

	website_foot();
}

static void
page_cart_add(char **matches)
{
	struct httpd_var_list *cookies = httpd_parse_cookies();
	char *val, new[1024], *env;

	if ((val = httpd_get_var(cookies, "item")) == NULL) {
		httpd_set_var(&httpd_cookies, "item", matches[0]);
	} else {
		snprintf(new, sizeof new, "%s %s", val, matches[0]);
		httpd_set_var(&httpd_cookies, "item", new);
	}
	if ((env = getenv("HTTP_REFERER")) == NULL)
		httpd_redirect(303, "/cart/");
	else
		httpd_redirect(303, env);
}

static void
page_cart_del(char **matches)
{
	struct httpd_var_list *cookies = httpd_parse_cookies();
	char new[1024], *val;

	if ((val = httpd_get_var(cookies, "item")) == NULL)
		httpd_fatal("no $item cookie");
	del_string(new, sizeof new, val, matches[0]);
	httpd_set_var(&httpd_cookies, "item", new);
	httpd_redirect(303, "/cart/");
}

static void
page_cart_done(char **matches)
{
	(void)matches;

	httpd_set_var(&httpd_cookies, "item", "");
	website_head("Paiement R??ussi");
	httpd_template("html/cart-done.html", &website);
	website_foot();
}

static void
page_cart_error(char **matches)
{
	(void)matches;

	website_head("Erreur de Paiement");
	httpd_template("html/cart-error.html", &website);
	website_foot();
}

static char *item_file;

static int
admin_loop_image(char *path)
{
	struct httpd_var_list vars = {0};

	assert(strchr(path, '/'));
	httpd_set_var(&vars, "item.file", item_file);
	httpd_set_var(&vars, "file", strrchr(path, '/') + 1);
	httpd_template("html/admin-image-edit.html", &vars);
	return 0;
}

static char *category_file;

static int
admin_loop_item(char *path)
{
	struct httpd_var_list vars = {0};

	httpd_read_var_list(&vars, path);
	item_file = httpd_get_var(&vars, "file");
	httpd_set_var(&vars, "category.file", category_file);
	printf("<article class=\"admin\">\n");
	httpd_template("html/admin-item-edit.html", &vars);
	loop(&vars, "image", admin_loop_image);
	httpd_template("html/admin-image-add.html", &vars);
	printf("</article>\n");
	return 0;
}

static int
admin_loop_category(char *path)
{
	struct httpd_var_list vars = {0};

	httpd_read_var_list(&vars, path);
	category_file = httpd_get_var(&vars, "file");
	httpd_template("html/admin-category-edit.html", &vars);
	loop(&vars, "item", admin_loop_item);
	httpd_template("html/admin-item-add.html", &vars);
	return 0;
}

static void
page_admin(char **matches)
{
	struct httpd_var_list category = {0};
	(void)matches;

	website_head("Administration");
	loop(&website, "category", admin_loop_category);
	httpd_template("html/admin-category-add.html", &category);
	website_foot();
}

static void
page_admin_category_add(char **matches)
{
	(void)matches;

	payload_as_child("website", "category");
	httpd_redirect(303, "/admin/");
}

static void
page_admin_category_edit(char **matches)
{
	struct httpd_var_list *payload = httpd_parse_payload();
	char path[64];

	snprintf(path, sizeof path, "db/category/%s", matches[0]);
	httpd_write_var_list(payload, path);
	httpd_redirect(303, "/admin/");
}

static void
page_admin_category_del(char **matches)
{
	del_id("website", "category", matches[0]);
	httpd_redirect(303, "/admin/");
}

static void
page_admin_item_add(char **matches)
{
	char parent[64];

	snprintf(parent, sizeof parent, "category/%s", matches[0]);
	payload_as_child(parent, "item");
	httpd_redirect(303, "/admin/");
}

static void
page_admin_item_edit(char **matches)
{
	struct httpd_var_list *payload = httpd_parse_payload();
	char path[64];

	snprintf(path, sizeof path, "db/item/%s", matches[0]);
	httpd_write_var_list(payload, path);
	httpd_redirect(303, "/admin/");
}

static void
page_admin_item_del(char **matches)
{
	char parent[64];

	snprintf(parent, sizeof parent, "category/%s", matches[0]);
	del_id(parent, "item", matches[1]);
	httpd_redirect(303, "/admin/");
}

static void
page_admin_image_add(char **matches)
{
	char parent[64];

	snprintf(parent, sizeof parent, "item/%s", matches[0]);
	payload_as_file(parent, "image");
	httpd_redirect(303, "/admin/");
}

static void
page_admin_image_del(char **matches)
{
	char parent[64];

	snprintf(parent, sizeof parent, "item/%s", matches[0]);
	del_id(parent, "image", matches[1]);
	httpd_redirect(303, "/admin/");
}


static struct httpd_handler handlers[] = {
	{ HTTPD_GET,	"/",				page_home },
	{ HTTPD_GET,	"/item/*/",			page_item },
	{ HTTPD_GET,	"/cart/",			page_cart },
	{ HTTPD_POST,	"/cart/add/*/",			page_cart_add },
	{ HTTPD_POST,	"/cart/del/*/",			page_cart_del },
	{ HTTPD_GET,	"/cart/done/",			page_cart_done },
	{ HTTPD_GET,	"/cart/error/",			page_cart_error },
	{ HTTPD_GET,	"/admin/",			page_admin },
	{ HTTPD_POST,	"/admin/category/add/",		page_admin_category_add },
	{ HTTPD_POST,	"/admin/category/edit/*/",	page_admin_category_edit },
	{ HTTPD_POST,	"/admin/category/del/*/",	page_admin_category_del },
	{ HTTPD_POST,	"/admin/item/add/*/",		page_admin_item_add },
	{ HTTPD_POST,	"/admin/item/edit/*/",		page_admin_item_edit },
	{ HTTPD_POST,	"/admin/item/del/*/*/",		page_admin_item_del },
	{ HTTPD_POST,	"/admin/image/add/*/",		page_admin_image_add },
	{ HTTPD_POST,	"/admin/image/del/*/*/",	page_admin_image_del },
	{ HTTPD_ANY,	"*",				error_404 },
	{ HTTPD_ANY,	NULL,				NULL },
};

int
main(void)
{
	/* restrict allowed paths */
	unveil("html", "r");
	unveil("tmp", "rwc");
	unveil("db", "rwc");

	/* restrict allowed system calls */
	pledge("stdio rpath wpath cpath", NULL);

	/* handle the request with the handlers */
	httpd_handle_request(handlers);
	return 0;
}
