# Protocolo

    Aqui vamos a especificar la gramatica de cada comando, diciendo que hace y como ejecutarlo, y su codigo de respuesta, algunos ejemplos, extensiones propias y los casos borde que se decidieron.

  *NICK <apodo>*: Registra el apodo de una persona en el servidor (201), si la persona tiene un correo, lo registra da (303). En caso de que el servicio no responda temporalmente, retorna  433, o si la falla 

  *JOIN <#canal>*: Entra en algun canal, si no existe, lo crea. (210). En caso de que salga mal, retorna 501 si es sintaxis invalida, o 530, si no te has registrado aun con NICK. 

  *PART <#canal>*: Sale del canal, si no hay errores retorna 211. Retorna 442 si no eres miembro de un canal. Retorna 551 si el canal no existe

  *MSG <#canal> <texto>*:  Envia a al canal <#canal> un mensaje <texto>. Si sale bien, retorna 250. Si no eres miembro de ese canal retorna 442, si hay un error de sintaxis retorna 501

  *PRIV <apodo> <texto>*: ENvia un mensaje privado <texto> a la persona <apodo>, si la persona esta conectada recibe 301, si esta desconectada recibe 251 y se guarda en su buzon. Si el apodo no existe, retorna 550

  *CHANS*: Retorna la lista de canales con su cantidad de miembros, si sale bien retorna 200. Si sale mal porque el canal no existe retorna 551

  *WHO <#canal>*: Lista los miembros activos de un canal, si sale bien retorna 213, multilinea. Si el servidor no existe retorna 551

  *STAT*: Cantidad de mensajes en el buzon y su tamano en bytes, si sale bien retorna 220 <n> <bytes>, donde n es el numero de mensajes en buzon y bytes es su tamano en bytes, si el canal no existe retorna 551

  *LIST*: Retorna un renglon de que retorna <numero> <bytes> por mensaje 221 si sale bien. Si el canal no existe retonra 551. Donde <numero> es el numero del mensaje y <bytes> es su tamano, si sae bien retorna 221, si no existe ese canal 551. Supongo que esto deberia hacer por canal. Consideremos agregar este atributo. 

  *RETR <n>*: Devuelve un mensaje <n> completo, en formato RFC 5322, es multilinea. Si no existe el numero del mensaje en el buzon retorna 552

  *DELE <n>*: Marca un mensaje <n> para que sea borrado al cerrar el servidor.  Si sale bien retorna 223. Si no sale bien retorna 552, indicando quee el numero del mensaje no existe


  # Codigos de respuesta adicionales: 

  *300 <#canal> <apodo> <texto>*: cuando el mensaje se publica en un canal donde el cliente es miembro. 

  *301 <apodo> <texto>*: Mensaje privado y recibido en vivo

  *302 <#canal> <apodo> <evento>*: es un aviso, que indica si el cliente se desconecta, entra o sale del canal

  *303 <n>*: <n> es el numero de mensajes que el cliente tiene en su canal. 

  *502*: la linema es demasiado larga, el server descarta los bytes hasta el otro clrf

  *421*: Servidor saturado, se cierra la conexion


# Protocolo usado 

  