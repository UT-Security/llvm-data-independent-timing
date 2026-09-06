#!/usr/bin/env bash
#
# Experiment 11: the two applications php-src's harness benchmarks, prepared the way its
# benchmark/benchmark.php prepares them, with our php. Also the database WordPress needs.
#
#   setup_apps.sh fetch     clone php/benchmarking-wordpress-6.2 and php/benchmarking-symfony-demo-2.2.3
#                           at the commits the numbers were taken on (wp-cli.phar ships in the repo)
#   setup_apps.sh db        initialise (first run) and start MariaDB on 127.0.0.1:$DB_PORT with the
#                           wordpress/wordpress user the harness's docker-compose creates
#   setup_apps.sh install   wp-cli core install (the harness's command), pretty permalinks so the
#                           REST routes resolve, one warm-up request; Symfony cache:clear + cache:warmup
#   setup_apps.sh stopdb    stop that MariaDB
#   setup_apps.sh all       fetch, db, install
#
# MariaDB rather than MySQL: Homebrew's MySQL 26.7 authenticates with caching_sha2, which
# mysqlnd cannot do without OpenSSL, and this PHP is built --without-openssl so that no
# crypto lives in a prebuilt library. The database is a separate process, outside every arm.
#
# Env: W (work dir, default ~/Documents/dit-phpsuite), DB_PORT (default 3307)
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="${W:-$HOME/Documents/dit-phpsuite}"
DB_PORT="${DB_PORT:-3307}"
export WORDPRESS_DB_HOST="127.0.0.1:$DB_PORT"
PHP="$W/phpf-base/sapi/cli/php"; WP="$W/apps/wordpress"; SF="$W/apps/symfony-demo"
WP_REPO=https://github.com/php/benchmarking-wordpress-6.2.git;      WP_PIN=ef263dad5e1e6bbc78885cb6707c0a4d07ad5fc6
SF_REPO=https://github.com/php/benchmarking-symfony-demo-2.2.3.git; SF_PIN=b482c3e3dd48e4df21d0cb81e26af17a7fe35d24
MDB="$(brew --prefix mariadb 2>/dev/null)"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
wp() { ( cd "$WP" && "$PHP" -d error_reporting=0 wp-cli.phar "$@" ); }
mdb_ping() { "$MDB/bin/mariadb-admin" --protocol=tcp -h127.0.0.1 -P"$DB_PORT" -uroot ping >/dev/null 2>&1; }

do_fetch() {
  mkdir -p "$W/apps"
  for spec in "wordpress $WP_REPO $WP_PIN" "symfony-demo $SF_REPO $SF_PIN"; do
    set -- $spec
    if [[ -d "$W/apps/$1/.git" ]]; then info "$1: present"; else info "$1: cloning $2"; git clone -q "$2" "$W/apps/$1" || die "clone failed"; fi
    git -C "$W/apps/$1" checkout -q "$3" || die "$1: cannot check out $3"
    info "    $1 at $(git -C "$W/apps/$1" rev-parse --short HEAD)"
  done
  [[ -f "$WP/wp-cli.phar" ]] || die "wp-cli.phar missing from the WordPress repo"
}

do_db() {
  [[ -n "$MDB" ]] || die "brew install mariadb"
  if [[ ! -d "$W/mariadb-data/mysql" ]]; then
    info "MariaDB: initialising $W/mariadb-data"
    "$MDB/bin/mariadb-install-db" --datadir="$W/mariadb-data" --auth-root-authentication-method=normal > "$W/mariadb-init.log" 2>&1 || die "mariadb-install-db failed: $W/mariadb-init.log"
  fi
  if mdb_ping; then info "MariaDB: already up on 127.0.0.1:$DB_PORT"; else
    info "MariaDB: starting on 127.0.0.1:$DB_PORT"
    nohup "$MDB/bin/mariadbd" --datadir="$W/mariadb-data" --port="$DB_PORT" --bind-address=127.0.0.1 \
        --socket="$W/mariadb.sock" --pid-file="$W/mariadb.pid" --log-error="$W/mariadb-error.log" >/dev/null 2>&1 &
    for i in $(seq 1 30); do mdb_ping && break; sleep 1; done
    mdb_ping || die "MariaDB did not come up: $W/mariadb-error.log"
  fi
  "$MDB/bin/mariadb" --protocol=tcp -h127.0.0.1 -P"$DB_PORT" -uroot -e "
    CREATE DATABASE IF NOT EXISTS wordpress;
    CREATE USER IF NOT EXISTS 'wordpress'@'127.0.0.1' IDENTIFIED BY 'wordpress';
    CREATE USER IF NOT EXISTS 'wordpress'@'localhost' IDENTIFIED BY 'wordpress';
    GRANT ALL ON wordpress.* TO 'wordpress'@'127.0.0.1'; GRANT ALL ON wordpress.* TO 'wordpress'@'localhost'; FLUSH PRIVILEGES;" \
    || die "cannot create the wordpress database/user"
  info "MariaDB: database wordpress, user wordpress/wordpress"
}

do_install() {
  [[ -x "$PHP" ]] || die "no php at $PHP (build_php.sh base first)"
  mdb_ping || die "MariaDB is not up on 127.0.0.1:$DB_PORT (setup_apps.sh db)"
  info "WordPress: wp-cli core install (the harness's command)"
  wp core install --url=wordpress.local --title="Wordpress" --admin_user=wordpress --admin_password=wordpress \
     --admin_email=benchmark@php.net 2>&1 | tail -2
  wp option get siteurl 2>/dev/null | grep -q wordpress.local || die "WordPress install failed"
  info "WordPress: pretty permalinks (the REST routes need them)"
  wp rewrite structure '/%postname%/' 2>/dev/null | tail -1
  [[ "$(wp option get permalink_structure 2>/dev/null)" == "/%postname%/" ]] || die "permalink structure not set"
  rm -f "$WP/wp-content/mu-plugins/phpass-rounds.php"
  info "WordPress: one front-page run through index.php (the harness does this too)"
  ( cd "$WP" && "$PHP" index.php 2>/dev/null | wc -c | tr -d ' ' | sed 's/^/    body bytes: /' )
  info "Symfony Demo: cache:clear + cache:warmup (prod)"
  ( cd "$SF" && "$PHP" bin/console cache:clear 2>&1 | tail -1 && "$PHP" bin/console cache:warmup 2>&1 | tail -1 )
  ( cd "$SF" && "$PHP" -r '$db=new PDO("sqlite:data/database.sqlite"); foreach($db->query("select username, substr(password,1,7) as h from symfony_demo_user") as $r) echo "    user ", $r["username"], " ", $r["h"], "\n";' )
}

do_stopdb() {
  if [[ -f "$W/mariadb.pid" ]]; then kill "$(cat "$W/mariadb.pid")" 2>/dev/null && info "MariaDB stopped" || info "MariaDB was not running"; fi
}

case "${1:-all}" in
  fetch) do_fetch ;; db) do_db ;; install) do_install ;; stopdb) do_stopdb ;;
  all) do_fetch && do_db && do_install ;;
  *) die "usage: $0 fetch|db|install|stopdb|all" ;;
esac
info "done"
