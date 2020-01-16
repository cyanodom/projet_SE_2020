//  Interface pour le module pool_thread dans le cas d'un objet permettant la
//    gestion d'un pool de threads synchronisés dont le nombre est borné et la
//    communication avec ceux-ci est géré par le biais d'un espace de mémoire
//    partagée. Ces fameux threads sont capable de lancer une commande envoyée
//    via l'espace de mémoire partagée du type de la structure contenue dans le
//    fichier shm.h. La taille du tampon de caractères est donnée lors de la
//    création de l'objet.

#ifndef POOL_THREAD__H
  #define POOL_THREAD__H

  #include <pthread.h>

  #define POOL_THREAD_SUCCESS 0
  #define POOL_THREAD_FAILURE -1
  #define POOL_TRHEAD_NO_MORE_THREAD -2
  #define POOL_THREAD_BAD_ALLOC -3
  #define POOL_THREAD_SYNC_ERROR -4
  #define POOL_THREAD_BAD_FAILURE -5

  //  pool_thread, struct pool_thread : structure capable de stocker les
  //    informations utiles à l'uttilisation correcte de ce module
  typedef struct pool_thread pool_thread;

  //  les fonctions qui suivent ont un comportement indéterminé si leur
  //    paramètre de type pool_thread * ou pool_thread ** à pour valeur NULL.
  //    De même elle ont également un comportement indéterminé si ce même
  //    paramètre n'est pas l'adresse ou l'adresse de l'adresse d'un objet
  //    créé par la fonction pool_thread_init et non révoqué depuis par la
  //    fonction pool_thread_dispose. Cette dernière règle souffre néanmoins
  //    d'une exception : la fonction pool_thread_init accepte tout type
  //    d'objets étant l'addresse d'un objet de type pool_thread * valable
  //    et pool_thread_strerror ne possède pas ce genre de paramètres

  //  Toutes les fonctions de ce module renvoient un code d'erreur (à
  //    l'exception de pool_thread_strerror) et non leur résultat, de plus errno
  //    reste tel quel et permet alors plus de précisions quand à l'erreur, les
  //    codes d'erreur possibles des fonctions ainsi que les focntions capables
  //    de retourner ces codes sont explicités dans les lignes suivantes, de
  //    plus, la fonction pool_thread_strerror retourne une chaine de caratère
  //    expliquant grossièrement l'erreur survenue, selon les paramètres de
  //    compilation, ce module écrit automatiquement le résultat de cette
  //    fonction ainsi que celui de strerror de l'erreur originale et quelques
  //    informations suplémentaires. pour plus d'informations sur cette
  //    fonctionnalitée se référé au fichier macro.h

  //      POOL_THREAD_SUCCESS : aucune erreur détéctée
  //          (toutes les fonctions)
  //      POOL_THREAD_BAD_FAILURE : une grave erreur compromettante s'est
  //          produite, l'objet est dans un état instable il est conseillé
  //          de quitter le programme
  //          (pool_thread_dispose, pool_thread_enroll)
  //      POOL_THREAD_FAILURE : une erreur s'est produite, l'objet reste stable
  //          mais n'a pas pu effectuer une action, réessayez plus tard ...
  //          attention toutefois, cette erreur peut en entrainer des plus
  //          graves
  //          (pool_thread_enroll)
  //      POOL_TRHEAD_NO_MORE_THREAD : Un thread doit être enrôlé mais aucune
  //          place n'est disponible pour créer un thread ou aucun thread n'est
  //          actuellement libre
  //          (pool_thread_enroll)
  //      POOL_THREAD_SYNC_ERROR : Un objet de synchronisation a rencontré une
  //          erreur et n'a peut être pas été capable d'endossé son rôle
  //          correctement
  //          (pool_thread_dispose, pool_thread_enroll)
  //      POOL_THREAD_BAD_ALLOC : Une allocation dynamique a rencontré une
  //          erreur, probablement à cause d'un manque de mémoire
  //          (pool_thread_init, pool_thread_enroll)

  //  pool_thread_init : crée le pool de threads représenté par pool et
  //    l'initialise en fonction de son nombre minimal de threads min_thread_nb,
  //    de son nombre maximal de threads max_thread_nb, de la taille de son
  //    tampon de caratère shm_size, et du nombre maximum de
  //    connection / commandes envoyés à un thread en particulier avant son
  //    auto-destruction max_connect_per_thread
  extern int pool_thread_init(pool_thread **pool, size_t min_thread_nb,
      size_t max_thread_nb, size_t max_connect_per_thread, size_t shm_size);

  extern int pool_thread_dispose(pool_thread **pool);

  extern int pool_thread_enroll(pool_thread *pool, char *shm_name);

  extern int pool_thread_manage(pool_thread *pool);

  extern char *pool_thread_strerror(int errnum);
#endif
